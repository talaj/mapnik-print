#pragma once

#include <sstream>
#include <iomanip>
#include <fstream>
#include <memory>

#include <mapnik/map.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/image_reader.hpp>
#include <mapnik/util/variant.hpp>
#include <mapnik/agg_renderer.hpp>
#if defined(GRID_RENDERER)
#include <mapnik/grid/grid_renderer.hpp>
#endif

#include <mapnik/projection.hpp>

#if defined(HAVE_CAIRO)
#include <mapnik/cairo/cairo_renderer.hpp>
#include <mapnik/cairo/cairo_image_util.hpp>
#ifdef CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>
#endif
#ifdef CAIRO_HAS_PS_SURFACE
#include <cairo-ps.h>
#endif
#ifdef CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>
#endif
#endif

#include <boost/filesystem.hpp>

#ifndef HAVE_CAIRO
    Mapnik must be compiled with Cairo support
#endif

namespace mapnik_print
{

template <typename ImageType>
struct raster_renderer_base
{
    using image_type = ImageType;

    static constexpr const char * ext = ".png";
    static constexpr const bool support_tiles = true;

    void save(image_type const & image, boost::filesystem::path const& path) const
    {
        mapnik::save_to_file(image, path.string(), "png32");
    }
};

struct vector_renderer_base
{
    using image_type = std::string;

    static constexpr const bool support_tiles = false;

    void save(image_type const & image, boost::filesystem::path const& path) const
    {
        std::ofstream file(path.string().c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("Cannot open file for writing: " + path.string());
        }
        file << image;
    }
};

struct agg_renderer : raster_renderer_base<mapnik::image_rgba8>
{
    static constexpr const char * name = "agg";

    image_type render(mapnik::Map const & map, double scale_factor) const
    {
        image_type image(map.width(), map.height());
        mapnik::agg_renderer<image_type> ren(map, image, scale_factor);
        ren.apply();
        return image;
    }
};

struct cairo_renderer : raster_renderer_base<mapnik::image_rgba8>
{
    static constexpr const char * name = "cairo";

    image_type render(mapnik::Map const & map, double scale_factor) const
    {
        mapnik::cairo_surface_ptr image_surface(
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, map.width(), map.height()),
            mapnik::cairo_surface_closer());
        mapnik::cairo_ptr image_context(mapnik::create_context(image_surface));
        mapnik::cairo_renderer<mapnik::cairo_ptr> ren(map, image_context, scale_factor);
        ren.apply();
        image_type image(map.width(), map.height());
        mapnik::cairo_image_to_rgba8(image, image_surface);
        return image;
    }
};

using surface_create_type = cairo_surface_t *(&)(cairo_write_func_t, void *, double, double);

template <surface_create_type SurfaceCreateFunction>
struct cairo_vector_renderer : vector_renderer_base
{
    static constexpr double cairo_resolution = 72.0;

    static cairo_status_t write(void *closure,
                                const unsigned char *data,
                                unsigned int length)
    {
        std::ostringstream & ss = *reinterpret_cast<std::ostringstream*>(closure);
        ss.write(reinterpret_cast<char const *>(data), length);
        return ss ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
    }

    image_type render(mapnik::Map const & map, double scale_factor) const
    {
        std::ostringstream ss(std::stringstream::binary);
        mapnik::cairo_surface_ptr image_surface(
            SurfaceCreateFunction(write, &ss, map.width(), map.height()),
            mapnik::cairo_surface_closer());
        mapnik::cairo_ptr image_context(mapnik::create_context(image_surface));
        mapnik::cairo_renderer<mapnik::cairo_ptr> ren(map, image_context, scale_factor);
        ren.apply();
        cairo_surface_finish(&*image_surface);
        return ss.str();
    }
};

#ifdef CAIRO_HAS_SVG_SURFACE
struct cairo_svg_renderer : cairo_vector_renderer<cairo_svg_surface_create_for_stream>
{
    static constexpr const char * name = "cairo-svg";
    static constexpr const char * ext = ".svg";
};
#endif

#ifdef CAIRO_HAS_PS_SURFACE
struct cairo_ps_renderer : cairo_vector_renderer<cairo_ps_surface_create_for_stream>
{
    static constexpr const char * name = "cairo-ps";
    static constexpr const char * ext = ".ps";
};
#endif

#ifdef CAIRO_HAS_PDF_SURFACE
struct cairo_pdf_renderer : cairo_vector_renderer<cairo_pdf_surface_create_for_stream>
{
    static constexpr const char * name = "cairo-pdf";
    static constexpr const char * ext = ".pdf";
};
#endif

double meters_to_inches(double meters)
{
    return meters / 0.0254;
}

struct map_size
{
    double width, height;

    map_size meters_to_inches() const
    {
        return { mapnik_print::meters_to_inches(width),
                 mapnik_print::meters_to_inches(height) };
    }

    map_size operator *(double factor) const
    {
        return { width * factor, height * factor };
    }
};

double scale_merc(unsigned zoom)
{
    return (mapnik::EARTH_CIRCUMFERENCE / (1u << zoom)) / 256.0;
}

struct command
{
    map_size size;
    mapnik::box2d<double> extent;
    double scale_factor;
    double dpi;

    using point_type = mapnik::geometry::point<double>;

    static constexpr double points_per_inch = 72.0;

    command(
        point_type const & map_center,
        double scale_denom,
        map_size const & size,
        unsigned zoom,
        double dpi)
        : extent(0, 0, size.width, size.height),
          size(size.meters_to_inches() * points_per_inch),
          dpi(dpi)
    {
        mapnik::projection proj(srs);
        point_type geografic_center,;
        proj.inverse(geografic_center.x, geografic_center.y);

        double projection_scale_factor = std::cos(geografic_center.y * mapnik::D2R);
        extent *= scale_denom * projection_scale_factor;
        extent.re_center(map_center.x, map_center.y);

        double scale = scale_merc(zoom);
        double mapnik_scale_denom = mapnik::scale_denominator(scale, false);
        scale_factor = mapnik_scale_denom / (mapnik::scale_denominator(
            extent.width() / size.width, false));
    }
};

template <typename Renderer>
class renderer
{
    const Renderer ren;
    const boost::filesystem::path output_dir;
    mapnik::map map;

public:
    using renderer_type = Renderer;
    using image_type = typename Renderer::image_type;

    renderer(mapnik::map const & map)
        : map(map)
    {
    }

    image_type render(command const & cmd)
    {
        return ren.render(map, cmd.scale_factor);
    }
};


using renderer_type = mapnik::util::variant<renderer<agg_renderer>
#if defined(HAVE_CAIRO)
                                            ,renderer<cairo_renderer>
#ifdef CAIRO_HAS_SVG_SURFACE
                                            ,renderer<cairo_svg_renderer>
#endif
#ifdef CAIRO_HAS_PS_SURFACE
                                            ,renderer<cairo_ps_renderer>
#endif
#ifdef CAIRO_HAS_PDF_SURFACE
                                            ,renderer<cairo_pdf_renderer>
#endif
#endif
                                            >;

}

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

template <typename Renderer>
class renderer
{
public:
    using renderer_type = Renderer;
    using image_type = typename Renderer::image_type;

    renderer(boost::filesystem::path const & _output_dir)
        : ren(), output_dir(_output_dir)
    {
    }

    image_type render(mapnik::Map const & map, double scale_factor) const
    {
        return ren.render(map, scale_factor);
    }

    result report(image_type const & image,
                  std::string const & name,
                  map_size const & size,
                  map_size const & tiles,
                  double scale_factor) const
    {
        result res;

        res.state = STATE_OK;
        res.name = name;
        res.renderer_name = Renderer::name;
        res.scale_factor = scale_factor;
        res.size = size;
        res.tiles = tiles;

        boost::filesystem::create_directories(output_dir);
        boost::filesystem::path path = output_dir / image_file_name(name, size, tiles, scale_factor);
        res.image_path = path;
        ren.save(image, path);

        return res;
    }

private:
    std::string image_file_name(std::string const & test_name,
                                map_size const & size,
                                map_size const & tiles,
                                double scale_factor) const
    {
        std::stringstream s;
        s << test_name << '-' << (size.width / scale_factor) << '-' << (size.height / scale_factor) << '-';
        if (tiles.width > 1 || tiles.height > 1)
        {
            s << tiles.width << 'x' << tiles.height << '-';
        }
        s << std::fixed << std::setprecision(1) << scale_factor << '-' << Renderer::name;
        s << Renderer::ext;
        return s.str();
    }

    const Renderer ren;
    const boost::filesystem::path output_dir;
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

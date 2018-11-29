/*****************************************************************************
 *
 * This file is part of Mapnik (c++ mapping toolkit)
 *
 * Copyright (C) 2015 Artem Pavlenko
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *****************************************************************************/

#include "runner.hpp"
#include "config.hpp"

#include <mapnik/datasource_cache.hpp>
#include <mapnik/font_engine_freetype.hpp>

#include <boost/program_options.hpp>

#ifdef MAPNIK_LOG
using log_levels_map = std::map<std::string, mapnik::logger::severity_type>;

log_levels_map log_levels
{
    { "debug", mapnik::logger::severity_type::debug },
    { "warn",  mapnik::logger::severity_type::warn },
    { "error", mapnik::logger::severity_type::error },
    { "none",  mapnik::logger::severity_type::none }
};
#endif

using namespace mapnik_render;
namespace po = boost::program_options;

runner::renderer_container create_renderers(po::variables_map const & args,
                                            boost::filesystem::path const & output_dir,
                                            bool force_append = false)
{
    runner::renderer_container renderers;

    if (force_append || args.count(agg_renderer::name))
    {
        renderers.emplace_back(renderer<agg_renderer>(output_dir));
    }
#if defined(HAVE_CAIRO)
    if (force_append || args.count(cairo_renderer::name))
    {
        renderers.emplace_back(renderer<cairo_renderer>(output_dir));
    }
#ifdef CAIRO_HAS_SVG_SURFACE
    if (args.count(cairo_svg_renderer::name))
    {
        renderers.emplace_back(renderer<cairo_svg_renderer>(output_dir));
    }
#endif
#ifdef CAIRO_HAS_PS_SURFACE
    if (args.count(cairo_ps_renderer::name))
    {
        renderers.emplace_back(renderer<cairo_ps_renderer>(output_dir));
    }
#endif
#ifdef CAIRO_HAS_PDF_SURFACE
    if (args.count(cairo_pdf_renderer::name))
    {
        renderers.emplace_back(renderer<cairo_pdf_renderer>(output_dir));
    }
#endif
#endif
#if defined(SVG_RENDERER)
    if (force_append || args.count(svg_renderer::name))
    {
        renderers.emplace_back(renderer<svg_renderer>(output_dir));
    }
#endif
#if defined(GRID_RENDERER)
    if (force_append || args.count(grid_renderer::name))
    {
        renderers.emplace_back(renderer<grid_renderer>(output_dir));
    }
#endif

    if (renderers.empty())
    {
        return create_renderers(args, output_dir, true);
    }

    return renderers;
}

int main(int argc, char** argv)
{
    po::options_description desc("mapnik-render");
    desc.add_options()
        ("help,h", "produce usage message")
        ("verbose,v", "verbose output")
        ("duration,d", "output rendering duration")
        ("iterations,i", po::value<std::size_t>()->default_value(1), "number of iterations for benchmarking")
        ("output-dir", po::value<std::string>()->default_value("./"), "directory for output files")
        ("styles", po::value<std::vector<std::string>>(), "selected styles to test")
        ("fonts", po::value<std::string>()->default_value("fonts"), "font search path")
        ("plugins", po::value<std::string>()->default_value("plugins/input"), "input plugins search path")
#ifdef MAPNIK_LOG
        ("log", po::value<std::string>()->default_value(std::find_if(log_levels.begin(), log_levels.end(),
             [](log_levels_map::value_type const & level) { return level.second == mapnik::logger::get_severity(); } )->first),
             "log level (debug, warn, error, none)")
#endif
        ("scale-factor,s", po::value<std::vector<double>>()->default_value({ 1.0 }, "1.0"), "scale factor")
        ("envelope", po::value<std::string>(), "bounding box in map coordinates")
        ("size", po::value<std::string>(), "size of output images")
        (agg_renderer::name, "render with AGG renderer")
#if defined(HAVE_CAIRO)
        (cairo_renderer::name, "render with Cairo renderer")
#ifdef CAIRO_HAS_SVG_SURFACE
        (cairo_svg_renderer::name, "render with Cairo SVG renderer")
#endif
#ifdef CAIRO_HAS_PS_SURFACE
        (cairo_ps_renderer::name, "render with Cairo PS renderer")
#endif
#ifdef CAIRO_HAS_PDF_SURFACE
        (cairo_pdf_renderer::name, "render with Cairo PDF renderer")
#endif
#endif
#if defined(SVG_RENDERER)
        (svg_renderer::name, "render with SVG renderer")
#endif
#if defined(GRID_RENDERER)
        (grid_renderer::name, "render with Grid renderer")
#endif
        ;

    po::positional_options_description p;
    p.add("styles", -1);
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        std::clog << desc << std::endl;
        return 1;
    }

#ifdef MAPNIK_LOG
    std::string log_level(vm["log"].as<std::string>());
    log_levels_map::const_iterator level_iter = log_levels.find(log_level);
    if (level_iter == log_levels.end())
    {
        std::cerr << "Error: Unknown log level: " << log_level << std::endl;
        return 1;
    }
    else
    {
        mapnik::logger::set_severity(level_iter->second);
    }
#endif

    mapnik::freetype_engine::register_fonts(vm["fonts"].as<std::string>(), true);
    mapnik::datasource_cache::instance().register_datasources(vm["plugins"].as<std::string>());

    boost::filesystem::path output_dir(vm["output-dir"].as<std::string>());

    config defaults;
    defaults.scales = vm["scale-factor"].as<std::vector<double>>();

    if (vm.count("envelope"))
    {
        mapnik::box2d<double> box;
        box.from_string(vm["envelope"].as<std::string>());
        defaults.envelopes.push_back(box);
    }

    if (vm.count("size"))
    {
        const map_sizes_grammar<std::string::const_iterator> map_sizes_parser;
        std::string size(vm["size"].as<std::string>());
        parse_map_sizes(map_sizes_parser, size, defaults.sizes);
    }

    runner run(defaults,
               vm["iterations"].as<std::size_t>(),
               create_renderers(vm, output_dir));

    bool show_duration = vm.count("duration");
    report_type report(vm.count("verbose") ?
        report_type((console_report(show_duration))) :
        report_type((console_short_report(show_duration))));
    result_list results;

    if (!vm.count("styles"))
    {
        std::cerr << "Error: no input styles." << std::endl;
        return EXIT_FAILURE;
    }

    try
    {
        results = run.test(vm["styles"].as<std::vector<std::string>>(), report);
    }
    catch (std::exception & e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    unsigned failed_count = mapnik::util::apply_visitor(summary_visitor(results), report);

    return failed_count;
}

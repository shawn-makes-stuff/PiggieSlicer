#include "Slic3r/App/AppConfigProvider.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem.hpp>

namespace Slic3r::App {


std::string AppConfigProvider::last_used_physical_printer() const
{
    return AppServices::instance().app_config().get<std::string>("last_used_physical_printer");
}

void AppConfigProvider::set_last_used_physical_printer(const std::string& uuid)
{
    AppServices::instance().app_config_interactor().set_item_value(
        "last_used_physical_printer",
        Domain::ConfigValue(uuid)
    );
}

bool AppConfigProvider::get_show_step_import_parameters() const
{
    return AppServices::instance().app_config().get<bool>("show_step_import_parameters");
}

void AppConfigProvider::set_show_step_import_parameters(bool show)
{
    return AppServices::instance().app_config_interactor().set_item_value(
        "show_step_import_parameters",
        Domain::ConfigValue(show)
    );
}

double AppConfigProvider::get_step_linear_precision() const
{
    return AppServices::instance().app_config().get<double>("step_linear_precision");
}

void AppConfigProvider::set_step_linear_precision(double precision)
{
    AppServices::instance().app_config_interactor().set_item_value(
        "step_linear_precision",
        Domain::ConfigValue(precision)
    );
}

double AppConfigProvider::get_step_angle_precision() const
{
    return AppServices::instance().app_config().get<double>("step_angle_precision");
}

void AppConfigProvider::set_step_angle_precision(double precision)
{
    AppServices::instance().app_config_interactor().set_item_value(
        "step_angle_precision",
        Domain::ConfigValue(precision)
    );
}


} // Slic3r::App

#pragma once

#include <string>

namespace Slic3r::Biz::Network {

class ServiceConfig
{
    ServiceConfig();

public:
    const std::string& preset_repo_url() const
    {
        return m_preset_repo_url;
    }

    void set_preset_repo_url(const std::string& url)
    {
        m_preset_repo_url = url;
    }

    std::string preset_repo_repos_url() const
    {
        return m_preset_repo_url + "/v2/repos";
    }

    bool webdev_enabled() const
    {
        return m_webdev_enabled;
    }

    void set_webdev_enabled(bool enabled)
    {
        m_webdev_enabled = enabled;
    }

    static ServiceConfig& instance();

private:
    std::string m_preset_repo_url;
    bool m_webdev_enabled{false};
};

} // namespace Slic3r::Biz::Network

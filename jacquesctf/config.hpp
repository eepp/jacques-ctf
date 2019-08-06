/*
 * Copyright (C) 2018 Philippe Proulx <eepp.ca> - All Rights Reserved
 *
 * Unauthorized copying of this file, via any medium, is strictly
 * prohibited. Proprietary and confidential.
 */

#ifndef _JACQUES_CONFIG_HPP
#define _JACQUES_CONFIG_HPP

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <boost/filesystem.hpp>

namespace jacques {

class CliError :
    public std::runtime_error
{
public:
    CliError(const std::string& msg);
};

/*
 * This object guarantees:
 *
 * * There's at least one file path.
 * * There are no duplicate file paths.
 * * All file paths exist.
 */
class Config
{
protected:
    Config();

public:
    virtual ~Config();
};

class InspectConfig :
    public Config
{
public:
    explicit InspectConfig(std::vector<boost::filesystem::path>&& paths);

    const std::vector<boost::filesystem::path>& paths() const noexcept
    {
        return _paths;
    }

private:
    const std::vector<boost::filesystem::path> _paths;
};

class PrintMetadataTextConfig :
    public Config
{
public:
    explicit PrintMetadataTextConfig(const boost::filesystem::path& path);

    const boost::filesystem::path& path() const noexcept
    {
        return _path;
    }

private:
    const boost::filesystem::path _path;
};

class PrintCliUsageConfig :
    public Config
{
public:
    PrintCliUsageConfig();
};

class PrintVersionConfig :
    public Config
{
public:
    PrintVersionConfig();
};

std::unique_ptr<const Config> configFromArgs(int argc, const char *argv[]);

} // namespace jacques

#endif // _JACQUES_CONFIG_HPP

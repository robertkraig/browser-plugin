// Copyright (c) 2013 Shotgun Software Inc.

#include <boost/assign.hpp>
#include <boost/filesystem.hpp>

#include "ProcessManager.h"

namespace fs = ::boost::filesystem;

#if defined(BOOST_POSIX_API)
#  define TANK_SCRIPT_NAME "tank"
#elif defined(BOOST_WINDOWS_API)
#  define TANK_SCRIPT_NAME "tank.bat"
#else
#  error "Unsupported platform."
#endif

void ProcessManager::VerifyArguments(const std::string &pipelineConfigPath, const std::string &command)
{
    try {
        if (!boost::starts_with(command, "shotgun"))
            throw FB::script_error("ExecuteTankCommand error");
    
        fs::path exec = pipelineConfigPath;
        if (!fs::is_directory(exec)) {
            std::string err = "Could not find the Tank Configuration on disk: " + exec.string();
            throw FB::script_error(err);
        }
    
        exec /= TANK_SCRIPT_NAME;
        if (!fs::is_regular_file(exec))
            throw FB::script_error("Could not find the Tank command on disk: " + exec.string());
    } catch (fs::filesystem_error &e) {
        std::string msg = std::string("Error finding the Tank command on disk: ") + e.what();
        throw FB::script_error(msg);
    }
}

bp::child ProcessManager::Launch(const std::string &exec, const std::vector<std::string> &arguments)
{
    bp::context ctx;

    ctx.environment = bp::self::get_environment();
    ctx.stdout_behavior = bp::capture_stream();
    ctx.stderr_behavior = bp::capture_stream();

    return bp::launch(exec, arguments, ctx);
}

FB::VariantMap ProcessManager::ExecuteTankCommand(
    const FB::BrowserHostPtr& host,
    const std::string &pipelineConfigPath,
    const std::string &command,
    const std::vector<std::string> &args)
{
    host->htmlLog("[ShotgunIntegration] ExecuteTankCommand");
    
    try {
        VerifyArguments(pipelineConfigPath, command);
    
        fs::path exec = pipelineConfigPath;
        exec /= TANK_SCRIPT_NAME;

        std::vector<std::string> arguments = boost::assign::list_of(exec.string())(command);
        arguments.insert(arguments.end(), args.begin(), args.end());

        bp::child child = Launch(exec.string(), arguments);
        bp::status status = child.wait();

        int retcode;
        if (status.exited())
            retcode = status.exit_status();
        else
            retcode = -1;

        std::string line;
        std::ostringstream ossStdout;
        bp::pistream &isStdout = child.get_stdout();
        while (std::getline(isStdout, line)) {
            ossStdout << line << std::endl;
        }
        
        std::ostringstream ossStderr;
        bp::pistream &isStderr = child.get_stderr();
        while (std::getline(isStderr, line)) {
            ossStderr << line << std::endl;
        }
        
        return FB::variant_map_of<std::string>
            ("retcode", retcode)
            ("out", ossStdout.str())
            ("err", ossStderr.str());
    } catch (std::exception &e) {
        // May be running in a non-main thread.  Avoid propagating exception
        return FB::variant_map_of<std::string>
            ("retcode", -1)
            ("out", std::string(""))
            ("err", std::string(e.what()));
    }
}

void ProcessManager::ExecuteTankCommandAsync(
        const FB::BrowserHostPtr& host,
        const std::string &pipelineConfigPath,
        const std::string &command,
        const std::vector<std::string> &args,
        const ExecuteTankCallback &cb)
{
    VerifyArguments(pipelineConfigPath, command);
    boost::thread cmdThread(&ProcessManager::_ExecuteTankCommandAsync, this, host, pipelineConfigPath, command, args, cb);
}

void ProcessManager::_ExecuteTankCommandAsync(
        const FB::BrowserHostPtr& host,
        const std::string &pipelineConfigPath,
        const std::string &command,
        const std::vector<std::string> &args,
        const ExecuteTankCallback &cb)
{
    FB::VariantMap results = ExecuteTankCommand(host, pipelineConfigPath, command, args);
    cb(results["retcode"].convert_cast<int>(), results["out"].convert_cast<std::string>(), results["err"].convert_cast<std::string>());
}
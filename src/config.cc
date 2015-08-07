#include "singletons.h"
#include "config.h"
#include "logging.h"
#include <exception>
#include "files.h"
#include "sourcefile.h"

MainConfig::MainConfig() {
  find_or_create_config_files();
  boost::property_tree::json_parser::read_json(Singleton::config_dir() + "config.json", cfg);
  Singleton::Config::window()->keybindings = cfg.get_child("keybindings");
  GenerateSource();
  GenerateTheme();
  GenerateDirectoryFilter();
  GenerateTerminalCommands();
}

void MainConfig::GenerateTheme() {
  auto config = Singleton::Config::theme();
  auto props = cfg.get_child("theme");
  for (auto &prop : props) {
    if (prop.first == "theme") config->theme = prop.second.get_value<std::string>();
    if (prop.first == "main") config->main = prop.second.get_value<std::string>();
  }
}

void MainConfig::find_or_create_config_files() {
  std::vector<std::string> files = {"config.json", "menu.xml", "plugins.py"};
  boost::filesystem::create_directories(boost::filesystem::path(Singleton::config_dir()));
  for (auto &file : files) {
    auto path = boost::filesystem::path(Singleton::config_dir() + file);
    if (!boost::filesystem::is_regular_file(path)) {
      if (file == "config.json") juci::filesystem::write(path, configjson);
      if (file == "plugins.py") juci::filesystem::write(path, pluginspy);
      if (file == "menu.xml") juci::filesystem::write(path, menuxml);
    }
  }
}

void MainConfig::GenerateSource() {
  auto source_cfg=Singleton::Config::source();
  DEBUG("Fetching source cfg");
  // boost::property_tree::ptree
  auto source_json = cfg.get_child("source");
  auto clang_types_json = source_json.get_child("clang_types");
  auto style_json = source_json.get_child("style");
  source_cfg->tab_size = source_json.get<unsigned>("tab_size");
  for (unsigned c = 0; c < source_cfg->tab_size; c++)
    source_cfg->tab+=" ";
  for (auto &i : style_json)
    source_cfg->tags[i.first]=i.second.get_value<std::string>();
  for (auto &i : clang_types_json)
    source_cfg->types[i.first]=i.second.get_value<std::string>();
  DEBUG("Source cfg fetched");
}

void MainConfig::GenerateTerminalCommands() {
  auto terminal_cfg=Singleton::Config::terminal();
  boost::property_tree::ptree source_json = cfg.get_child("project");
  boost::property_tree::ptree compile_commands_json = source_json.get_child("compile_commands");
  boost::property_tree::ptree run_commands_json = source_json.get_child("run_commands");
  for (auto &i : compile_commands_json)
    terminal_cfg->compile_commands.emplace_back(i.second.get_value<std::string>());
  for (auto &i : run_commands_json)
    terminal_cfg->run_command=(i.second.get_value<std::string>()); //TODO: run_commands array->one run_command?
}

void MainConfig::GenerateDirectoryFilter() {
  auto dir_cfg=Singleton::Config::directories();
  DEBUG("Fetching directory filter");
  boost::property_tree::ptree dir_json = cfg.get_child("directoryfilter");
  boost::property_tree::ptree ignore_json = dir_json.get_child("ignore");
  boost::property_tree::ptree except_json = dir_json.get_child("exceptions");
  for ( auto &i : except_json )
    dir_cfg->exceptions.emplace_back(i.second.get_value<std::string>());
  for ( auto &i : ignore_json )
    dir_cfg->ignored.emplace_back(i.second.get_value<std::string>());
  DEBUG("Directory filter fetched");
}

#include "config.h"
#include <exception>
#include "files.h"
#include <iostream>
#include "filesystem.h"
#include "terminal.h"
#include <algorithm>

Config::Config() {
  std::vector<std::string> environment_variables = {"JUCI_HOME", "HOME", "AppData"};
  char *ptr = nullptr;
  for (auto &variable : environment_variables) {
    ptr=std::getenv(variable.c_str());
    if (ptr!=nullptr && boost::filesystem::exists(ptr)) {
      home_path = ptr;
      home_juci_path = home_path / ".juci";
      break;
    }
  }
  if(home_juci_path.empty()) {
    std::string searched_envs = "[";
    for(auto &variable : environment_variables)
      searched_envs+=variable+", ";
    searched_envs.erase(searched_envs.end()-2, searched_envs.end());
    searched_envs+="]";
    throw std::runtime_error("One of these environment variables needs to point to a writable directory to save configuration: " + searched_envs);
  }
  
#ifdef _WIN32
     auto env_MSYSTEM_PREFIX=std::getenv("MSYSTEM_PREFIX");
     if(env_MSYSTEM_PREFIX!=nullptr)
       terminal.msys2_mingw_path=boost::filesystem::path(env_MSYSTEM_PREFIX);
#endif
}

void Config::load() {
  auto config_json = (home_juci_path/"config"/"config.json").string(); // This causes some redundant copies, but assures windows support
  try {
    find_or_create_config_files();
    boost::property_tree::json_parser::read_json(config_json, cfg);
    update_config_file();
    retrieve_config();
  }
  catch(const std::exception &e) {
    ::Terminal::get().print("Error: could not parse "+config_json+": "+e.what()+"\n", true);
    std::stringstream ss;
    ss << default_config_file;
    boost::property_tree::read_json(ss, cfg);
    retrieve_config();
  }
  cfg.clear();
}

void Config::find_or_create_config_files() {
  auto config_dir = home_juci_path/"config";
  auto config_json = config_dir/"config.json";

  boost::filesystem::create_directories(config_dir); // io exp captured by calling method

  if (!boost::filesystem::exists(config_json))
    filesystem::write(config_json, default_config_file);

  auto juci_style_path = home_juci_path/"styles";
  boost::filesystem::create_directories(juci_style_path); // io exp captured by calling method

  juci_style_path/="juci-light.xml";
  if(!boost::filesystem::exists(juci_style_path))
    filesystem::write(juci_style_path, juci_light_style);
  juci_style_path=juci_style_path.parent_path();
  juci_style_path/="juci-dark.xml";
  if(!boost::filesystem::exists(juci_style_path))
    filesystem::write(juci_style_path, juci_dark_style);
  juci_style_path=juci_style_path.parent_path();
  juci_style_path/="juci-dark-blue.xml";
  if(!boost::filesystem::exists(juci_style_path))
    filesystem::write(juci_style_path, juci_dark_blue_style);
}

void Config::retrieve_config() {
  auto keybindings_pt = cfg.get_child("keybindings");
  for (auto &i : keybindings_pt) {
    menu.keys[i.first] = i.second.get_value<std::string>();
  }
  get_source();

  window.theme_name=cfg.get<std::string>("gtk_theme.name");
  window.theme_variant=cfg.get<std::string>("gtk_theme.variant");
  window.version = cfg.get<std::string>("version");
  window.default_size = {cfg.get<int>("default_window_size.width"), cfg.get<int>("default_window_size.height")};
  
  project.default_build_path=cfg.get<std::string>("project.default_build_path");
  project.debug_build_path=cfg.get<std::string>("project.debug_build_path");
  project.make_command=cfg.get<std::string>("project.make_command");
  project.cmake_command=cfg.get<std::string>("project.cmake_command");
  project.save_on_compile_or_run=cfg.get<bool>("project.save_on_compile_or_run");
  project.clear_terminal_on_compile=cfg.get<bool>("project.clear_terminal_on_compile");
  project.ctags_command=cfg.get<std::string>("project.ctags_command");
  
  terminal.history_size=cfg.get<int>("terminal.history_size");
  terminal.font=cfg.get<std::string>("terminal.font");
  
  terminal.show_progress=cfg.get<bool>("terminal.show_progress");
  
  terminal.clang_format_command="clang-format";
#ifdef __linux
  if(terminal.clang_format_command=="clang-format" &&
     !boost::filesystem::exists("/usr/bin/clang-format") && !boost::filesystem::exists("/usr/local/bin/clang-format")) {
    if(boost::filesystem::exists("/usr/bin/clang-format-3.9"))
      terminal.clang_format_command="/usr/bin/clang-format-3.9";
    else if(boost::filesystem::exists("/usr/bin/clang-format-3.8"))
      terminal.clang_format_command="/usr/bin/clang-format-3.8";
    else if(boost::filesystem::exists("/usr/bin/clang-format-3.7"))
      terminal.clang_format_command="/usr/bin/clang-format-3.7";
    else if(boost::filesystem::exists("/usr/bin/clang-format-3.6"))
      terminal.clang_format_command="/usr/bin/clang-format-3.6";
    else if(boost::filesystem::exists("/usr/bin/clang-format-3.5"))
      terminal.clang_format_command="/usr/bin/clang-format-3.5";
  }
#endif
}

bool Config::add_missing_nodes(const boost::property_tree::ptree &default_cfg, std::string parent_path) {
  if(parent_path.size()>0)
    parent_path+=".";
  bool unchanged=true;
  for(auto &node: default_cfg) {
    auto path=parent_path+node.first;
    try {
      cfg.get<std::string>(path);
    }
    catch(const std::exception &e) {
      cfg.add(path, node.second.data());
      unchanged=false;
    }
    unchanged&=add_missing_nodes(node.second, path);
  }
  return unchanged;
}

bool Config::remove_deprecated_nodes(const boost::property_tree::ptree &default_cfg, boost::property_tree::ptree &config_cfg, std::string parent_path) {
  if(parent_path.size()>0)
    parent_path+=".";
  bool unchanged=true;
  for(auto it=config_cfg.begin();it!=config_cfg.end();) {
    auto path=parent_path+it->first;
    try {
      default_cfg.get<std::string>(path);
      unchanged&=remove_deprecated_nodes(default_cfg, it->second, path);
      ++it;
    }
    catch(const std::exception &e) {
      it=config_cfg.erase(it);
      unchanged=false;
    }
  }
  return unchanged;
}

void Config::update_config_file() {
  boost::property_tree::ptree default_cfg;
  bool cfg_ok=true;
  try {
    if(cfg.get<std::string>("version")!=JUCI_VERSION) {
      std::stringstream ss;
      ss << default_config_file;
      boost::property_tree::read_json(ss, default_cfg);
      cfg_ok=false;
      if(cfg.count("version")>0)
        cfg.find("version")->second.data()=default_cfg.get<std::string>("version");
      
      auto style_path=home_juci_path/"styles";
      filesystem::write(style_path/"juci-light.xml", juci_light_style);
      filesystem::write(style_path/"juci-dark.xml", juci_dark_style);
      filesystem::write(style_path/"juci-dark-blue.xml", juci_dark_blue_style);
    }
    else
      return;
  }
  catch(const std::exception &e) {
    std::cerr << "Error reading json-file: " << e.what() << std::endl;
    cfg_ok=false;
  }
  cfg_ok&=add_missing_nodes(default_cfg);
  cfg_ok&=remove_deprecated_nodes(default_cfg, cfg);
  if(!cfg_ok)
    boost::property_tree::write_json((home_juci_path/"config"/"config.json").string(), cfg);
}

void Config::get_source() {
  auto source_json = cfg.get_child("source");

  source.style=source_json.get<std::string>("style");
  source.font=source_json.get<std::string>("font");

  source.cleanup_whitespace_characters=source_json.get<bool>("cleanup_whitespace_characters");
  source.show_whitespace_characters=source_json.get<std::string>("show_whitespace_characters");
  
  source.smart_brackets=source_json.get<bool>("smart_brackets");
  source.smart_inserts=source_json.get<bool>("smart_inserts");
  if(source.smart_inserts)
    source.smart_brackets=true;

  source.show_map = source_json.get<bool>("show_map");
  source.map_font_size = source_json.get<std::string>("map_font_size");
  
  source.show_git_diff = source_json.get<bool>("show_git_diff");
  
  source.show_background_pattern = source_json.get<bool>("show_background_pattern");

  source.spellcheck_language = source_json.get<std::string>("spellcheck_language");

  source.default_tab_char = source_json.get<char>("default_tab_char");
  source.default_tab_size = source_json.get<unsigned>("default_tab_size");
  source.auto_tab_char_and_size = source_json.get<bool>("auto_tab_char_and_size");
  source.tab_indents_line = source_json.get<bool>("tab_indents_line");

  source.wrap_lines = source_json.get<bool>("wrap_lines");

  source.highlight_current_line = source_json.get<bool>("highlight_current_line");
  source.show_line_numbers = source_json.get<bool>("show_line_numbers");

  for (auto &i : source_json.get_child("clang_types")) {
    try {
      source.clang_types[std::stoi(i.first)] = i.second.get_value<std::string>();
    }
    catch(const std::exception &) {}
  }
  
  source.clang_format_style = source_json.get<std::string>("clang_format_style");
  
  auto pt_doc_search=cfg.get_child("documentation_searches");
  for(auto &pt_doc_search_lang: pt_doc_search) {
    source.documentation_searches[pt_doc_search_lang.first].separator=pt_doc_search_lang.second.get<std::string>("separator");
    auto &queries=source.documentation_searches.find(pt_doc_search_lang.first)->second.queries;
    for(auto &i: pt_doc_search_lang.second.get_child("queries")) {
      queries[i.first]=i.second.get_value<std::string>();
    }
  }
}

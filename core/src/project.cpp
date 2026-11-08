#include "ave/project.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ave {

Project::Project()
    : Project("Untitled Project") {}

Project::Project(std::string name)
    : Project("project-" + UUID::generate(), std::move(name), CURRENT_SCHEMA_VERSION) {}

Project::Project(std::string id, std::string name, std::string schema_version)
    : id_(std::move(id)),
      name_(std::move(name)),
      schema_version_(std::move(schema_version)),
      timeline_(),
      media_pool_(),
      command_history_() {}

bool Project::is_dirty() const noexcept {
    return command_history_.is_dirty();
}

void Project::mark_clean() noexcept {
    command_history_.mark_clean();
}

CommandResult Project::execute_command(std::unique_ptr<Command> cmd) {
    return command_history_.execute(std::move(cmd), timeline_, media_pool_);
}

CommandResult Project::undo() {
    return command_history_.undo(timeline_, media_pool_);
}

CommandResult Project::redo() {
    return command_history_.redo(timeline_, media_pool_);
}

std::string Project::to_json() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"schema_version\": \"" << schema_version_ << "\",\n";
    ss << "  \"id\": \"" << id_ << "\",\n";
    ss << "  \"name\": \"" << name_ << "\",\n";
    ss << "  \"timeline\": " << timeline_.to_json() << ",\n";
    ss << "  \"media_pool\": " << media_pool_.to_json() << "\n";
    ss << "}";
    return ss.str();
}

namespace {

std::string proj_extract_string(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++;
    std::string result;
    while (pos < json.length()) {
        if (json[pos] == '\\' && pos + 1 < json.length()) {
            pos++;
            switch (json[pos]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default: result += json[pos]; break;
            }
        } else if (json[pos] == '"') {
            break;
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

std::string extract_subobject(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    size_t start = json.find('{', pos);
    if (start == std::string::npos) return "";

    int count = 0;
    size_t end = start;
    for (; end < json.length(); ++end) {
        if (json[end] == '{') count++;
        else if (json[end] == '}') {
            count--;
            if (count == 0) break;
        }
    }
    if (count != 0) return "";
    return json.substr(start, end - start + 1);
}

} // namespace

Project Project::from_json(const std::string& json_text) {
    std::string ver = proj_extract_string(json_text, "schema_version");
    std::string id = proj_extract_string(json_text, "id");
    std::string name = proj_extract_string(json_text, "name");

    Project proj(id.empty() ? UUID::generate() : id,
                 name.empty() ? "Untitled Project" : name,
                 ver.empty() ? CURRENT_SCHEMA_VERSION : ver);

    std::string tl_json = extract_subobject(json_text, "timeline");
    if (!tl_json.empty()) {
        proj.timeline_ = Timeline::from_json(tl_json);
    }

    std::string mp_json = extract_subobject(json_text, "media_pool");
    if (!mp_json.empty()) {
        proj.media_pool_ = MediaPool::from_json(mp_json);
    }

    return proj;
}

bool Project::save(const std::filesystem::path& destination_path) {
    std::filesystem::path target = destination_path.empty() ? file_path_ : destination_path;
    if (target.empty()) {
        return false;
    }

    // Ensure parent directory exists
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path());
    }

    std::filesystem::path temp_path = target;
    temp_path += ".tmp";

    {
        std::ofstream ofs(temp_path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            return false;
        }
        std::string serialized = to_json();
        ofs.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        if (!ofs.good()) {
            return false;
        }
    }

    // Atomic replace
    std::error_code ec;
    std::filesystem::rename(temp_path, target, ec);
    if (ec) {
        // Fallback: remove and rename
        std::filesystem::remove(target, ec);
        std::filesystem::rename(temp_path, target, ec);
        if (ec) {
            return false;
        }
    }

    file_path_ = target;
    mark_clean();
    return true;
}

Project Project::load(const std::filesystem::path& source_path) {
    if (!std::filesystem::exists(source_path)) {
        throw std::runtime_error("Project file not found: " + source_path.string());
    }

    std::ifstream ifs(source_path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Failed to open project file: " + source_path.string());
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();

    Project proj = from_json(ss.str());
    proj.file_path_ = source_path;
    proj.mark_clean();
    return proj;
}

} // namespace ave

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/infrastructure/filesystem_input_file_storage.hpp"

namespace {

using biocore::infrastructure::FilesystemInputFileStorage;

class TemporaryTree final {
public:
    TemporaryTree() {
        const auto value = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("biocore-input-storage-" + std::to_string(value));
        project = root / std::filesystem::path{u8"proje-çalışma"};
        sources = root / "sources";
        std::filesystem::create_directories(project / "inputs");
        std::filesystem::create_directories(sources);
    }
    ~TemporaryTree() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    std::filesystem::path root;
    std::filesystem::path project;
    std::filesystem::path sources;
};

void write_bytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Unable to write test file");
}

[[nodiscard]] std::vector<unsigned char> read_bytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
#ifdef _WIN32
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const char raw_character : value) {
        encoded.push_back(static_cast<char8_t>(
            static_cast<unsigned char>(raw_character)
        ));
    }
    return std::filesystem::path{encoded};
#else
    return std::filesystem::path{value};
#endif
}

[[nodiscard]] std::string utf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] bool commit_and_collision_contract() {
    TemporaryTree tree;
    const auto first_source_dir = tree.sources / "a";
    const auto second_source_dir = tree.sources / "b";
    std::filesystem::create_directories(first_source_dir);
    std::filesystem::create_directories(second_source_dir);
    const auto first_source = first_source_dir / std::filesystem::path{u8"örnek.fastq"};
    const auto second_source = second_source_dir / std::filesystem::path{u8"örnek.fastq"};
    const auto reserved_name_source = tree.sources / ".biocore-import.0.tmp";
    const std::vector<unsigned char> first_bytes{0, 1, 2, 10, 255};
    const std::vector<unsigned char> second_bytes{9, 8, 7};
    write_bytes(first_source, first_bytes);
    write_bytes(second_source, second_bytes);
    write_bytes(reserved_name_source, {4, 5, 6});

    FilesystemInputFileStorage storage{utf8(std::filesystem::canonical(tree.project))};
    auto first = storage.prepare_managed_copy(utf8(first_source), "file-1");
    const auto first_prepared = first->prepared_file();
    const std::filesystem::path first_managed = path_from_utf8(first_prepared.managed_path);
    if (!std::filesystem::exists(first_managed) || read_bytes(first_managed) != first_bytes ||
        first_prepared.relative_project_path != "inputs/file-1/örnek.fastq" ||
        first_prepared.size_bytes != static_cast<std::int64_t>(first_bytes.size())) {
        return false;
    }
    first->commit();
    first.reset();
    if (!std::filesystem::exists(first_managed)) return false;

    auto second = storage.prepare_managed_copy(utf8(second_source), "file-2");
    const std::filesystem::path second_managed = path_from_utf8(second->prepared_file().managed_path);
    second->commit();
    second.reset();
    auto reserved = storage.prepare_managed_copy(utf8(reserved_name_source), "file-3");
    const std::filesystem::path reserved_managed = path_from_utf8(reserved->prepared_file().managed_path);
    reserved->commit();
    reserved.reset();
    return first_managed != second_managed && std::filesystem::exists(second_managed) &&
           read_bytes(second_managed) == second_bytes &&
           reserved_managed.filename() == ".biocore-import.0.tmp" &&
           read_bytes(reserved_managed) == std::vector<unsigned char>({4, 5, 6});
}

[[nodiscard]] bool rollback_contract() {
    TemporaryTree tree;
    const auto source = tree.sources / "rollback.bin";
    write_bytes(source, {1, 2, 3, 4});
    FilesystemInputFileStorage storage{utf8(std::filesystem::canonical(tree.project))};

    std::filesystem::path managed;
    {
        auto transaction = storage.prepare_managed_copy(utf8(source), "rollback-id");
        managed = path_from_utf8(transaction->prepared_file().managed_path);
        const auto untracked = managed.parent_path() / "user-note.txt";
        write_bytes(untracked, {5});
    }

    return !std::filesystem::exists(managed) &&
           std::filesystem::exists(managed.parent_path() / "user-note.txt") &&
           std::filesystem::exists(managed.parent_path());
}

[[nodiscard]] bool rejection_contract() {
    TemporaryTree tree;
    const auto source = tree.sources / "input.txt";
    write_bytes(source, {1});
    FilesystemInputFileStorage storage{utf8(std::filesystem::canonical(tree.project))};

    try {
        static_cast<void>(storage.prepare_managed_copy(utf8(source), "../escape"));
        return false;
    } catch (const std::invalid_argument&) {
    }

    try {
        static_cast<void>(storage.prepare_managed_copy(utf8(tree.sources), "directory"));
        return false;
    } catch (const std::invalid_argument&) {
    }

    std::filesystem::create_directory(tree.project / "inputs" / "occupied");
    try {
        static_cast<void>(storage.prepare_managed_copy(utf8(source), "occupied"));
        return false;
    } catch (const std::runtime_error&) {
    }

    const auto alias = tree.project / ".";
    try {
        FilesystemInputFileStorage invalid{utf8(alias)};
        return false;
    } catch (const std::invalid_argument&) {
    }

    const auto symlink_source = tree.sources / "source-link";
    std::error_code link_error;
    std::filesystem::create_symlink(source, symlink_source, link_error);
    if (!link_error) {
        try {
            static_cast<void>(storage.prepare_managed_copy(utf8(symlink_source), "symlink"));
            return false;
        } catch (const std::invalid_argument&) {
        }
    }
    return true;
}


[[nodiscard]] bool browser_upload_contract() {
    TemporaryTree tree;
    std::filesystem::create_directories(tree.project / ".biocore" / "runtime" / "browser-uploads" / "stale");
    write_bytes(
        tree.project / ".biocore" / "runtime" / "browser-uploads" / "stale" / "old.fa",
        {1, 2, 3}
    );

    FilesystemInputFileStorage storage{utf8(std::filesystem::canonical(tree.project))};
    if (std::filesystem::exists(
            tree.project / ".biocore" / "runtime" / "browser-uploads" / "stale"
        )) {
        return false;
    }

    if (!storage.begin_browser_upload("upload-1", "genome.fa") ||
        storage.begin_browser_upload("upload-1", "genome.fa")) {
        return false;
    }

    const std::string first{"AC\0", 3U};
    const std::string second{"GTN", 3U};
    if (storage.append_browser_upload("upload-1", 0U, first) != 3U) {
        return false;
    }
    try {
        static_cast<void>(storage.append_browser_upload("upload-1", 2U, "X"));
        return false;
    } catch (const std::runtime_error&) {
    }
    if (storage.append_browser_upload("upload-1", 3U, second) != 6U) {
        return false;
    }

    std::filesystem::path final_path;
    {
        auto transaction = storage.prepare_browser_upload_commit("upload-1", "file-1");
        final_path = path_from_utf8(transaction->prepared_file().managed_path);
        if (!std::filesystem::exists(final_path) ||
            transaction->prepared_file().relative_project_path != "inputs/file-1/genome.fa") {
            return false;
        }
    }
    if (std::filesystem::exists(final_path)) return false;

    auto retry = storage.prepare_browser_upload_commit("upload-1", "file-2");
    final_path = path_from_utf8(retry->prepared_file().managed_path);
    if (read_bytes(final_path) != std::vector<unsigned char>({'A','C',0,'G','T','N'})) {
        return false;
    }
    retry->commit();
    retry.reset();
    if (!std::filesystem::exists(final_path) ||
        std::filesystem::exists(
            tree.project / ".biocore" / "runtime" / "browser-uploads" / "upload-1"
        )) {
        return false;
    }

    if (!storage.begin_browser_upload("upload-2", "cancel.fa")) return false;
    static_cast<void>(storage.append_browser_upload("upload-2", 0U, "A"));
    storage.discard_browser_upload("upload-2");
    if (std::filesystem::exists(
            tree.project / ".biocore" / "runtime" / "browser-uploads" / "upload-2"
        )) {
        return false;
    }

    try {
        static_cast<void>(storage.begin_browser_upload("upload-3", "../escape.fa"));
        return false;
    } catch (const std::invalid_argument&) {
    }
    return true;
}

}  // namespace

int main() {
    if (!commit_and_collision_contract() || !rollback_contract() || !rejection_contract() ||
        !browser_upload_contract()) {
        std::cerr << "FilesystemInputFileStorage contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_path_canonicalizer.hpp"
#include "biocore/application/i_project_repository.hpp"
#include "biocore/application/i_project_workspace.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/project_service.hpp"
#include "biocore/application/project_service_error.hpp"
#include "biocore/domain/project.hpp"

namespace {

using biocore::application::CreateProjectRequest;
using biocore::application::DuplicateProjectRootError;
using biocore::application::IIdGenerator;
using biocore::application::IPathCanonicalizer;
using biocore::application::IProjectRepository;
using biocore::application::IProjectWorkspace;
using biocore::application::IProjectWorkspaceTransaction;
using biocore::application::IUtcClock;
using biocore::application::ProjectIdGenerationError;
using biocore::application::ProjectService;
using biocore::domain::Project;

class FakeProjectRepository final : public IProjectRepository {
public:
    explicit FakeProjectRepository(std::vector<std::string>* events = nullptr) : events_{events} {}

    void save(const Project& project) override {
        record("repository.save");
        ++save_calls;
        if (throw_on_save) {
            throw std::runtime_error{"repository save failed"};
        }
        saved_project = project;
    }

    [[nodiscard]] std::optional<Project> find_by_id(const std::string_view project_id) override {
        record("repository.find_by_id");
        for (const Project& project : projects) {
            if (project.id() == project_id) {
                return project;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Project> find_by_root_path(const std::string_view root_path) override {
        record("repository.find_by_root_path");
        last_root_lookup = std::string{root_path};
        for (const Project& project : projects) {
            if (project.root_path() == root_path) {
                return project;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<Project> list() override {
        return projects;
    }

    bool remove(const std::string_view project_id) override {
        for (auto iterator = projects.begin(); iterator != projects.end(); ++iterator) {
            if (iterator->id() == project_id) {
                projects.erase(iterator);
                return true;
            }
        }
        return false;
    }

    std::vector<Project> projects;
    std::optional<Project> saved_project;
    std::string last_root_lookup;
    std::size_t save_calls{0U};
    bool throw_on_save{false};

private:
    void record(const std::string_view event) {
        if (events_ != nullptr) {
            events_->emplace_back(event);
        }
    }

    std::vector<std::string>* events_;
};

class FakeIdGenerator final : public IIdGenerator {
public:
    FakeIdGenerator(std::deque<std::string> values, std::vector<std::string>* events = nullptr)
        : values_{std::move(values)}, events_{events} {}

    [[nodiscard]] std::string generate() override {
        if (events_ != nullptr) {
            events_->emplace_back("id.generate");
        }
        ++calls;
        if (values_.empty()) {
            return "fallback-id";
        }
        std::string value = std::move(values_.front());
        values_.pop_front();
        return value;
    }

    std::size_t calls{0U};

private:
    std::deque<std::string> values_;
    std::vector<std::string>* events_;
};

class FakeUtcClock final : public IUtcClock {
public:
    explicit FakeUtcClock(std::string timestamp, std::vector<std::string>* events = nullptr)
        : timestamp_{std::move(timestamp)}, events_{events} {}

    [[nodiscard]] std::string now_utc_iso8601() override {
        if (events_ != nullptr) {
            events_->emplace_back("clock.now");
        }
        ++calls;
        return timestamp_;
    }

    std::size_t calls{0U};

private:
    std::string timestamp_;
    std::vector<std::string>* events_;
};

class FakePathCanonicalizer final : public IPathCanonicalizer {
public:
    explicit FakePathCanonicalizer(std::string canonical_path, std::vector<std::string>* events = nullptr)
        : canonical_path_{std::move(canonical_path)}, events_{events} {}

    [[nodiscard]] std::string canonicalize_existing_directory(const std::string_view path) override {
        if (events_ != nullptr) {
            events_->emplace_back("path.canonicalize");
        }
        ++calls;
        last_input = std::string{path};
        return canonical_path_;
    }

    std::size_t calls{0U};
    std::string last_input;

private:
    std::string canonical_path_;
    std::vector<std::string>* events_;
};

class ThrowingPathCanonicalizer final : public IPathCanonicalizer {
public:
    [[nodiscard]] std::string canonicalize_existing_directory(std::string_view) override {
        ++calls;
        throw std::runtime_error{"canonicalization failed"};
    }

    std::size_t calls{0U};
};

struct WorkspaceState final {
    std::size_t initialize_calls{0U};
    std::size_t commit_calls{0U};
    std::size_t rollback_calls{0U};
    std::optional<Project> initialized_project;
    std::vector<std::string>* events{nullptr};
};

class FakeWorkspaceTransaction final : public IProjectWorkspaceTransaction {
public:
    explicit FakeWorkspaceTransaction(WorkspaceState& state) : state_{state} {}

    ~FakeWorkspaceTransaction() override {
        if (!committed_) {
            ++state_.rollback_calls;
            record("workspace.rollback");
        }
    }

    void commit() noexcept override {
        committed_ = true;
        ++state_.commit_calls;
        record("workspace.commit");
    }

private:
    void record(const std::string_view event) const {
        if (state_.events != nullptr) {
            state_.events->emplace_back(event);
        }
    }

    WorkspaceState& state_;
    bool committed_{false};
};

class FakeProjectWorkspace final : public IProjectWorkspace {
public:
    explicit FakeProjectWorkspace(std::vector<std::string>* events = nullptr) {
        state.events = events;
    }

    [[nodiscard]] std::unique_ptr<IProjectWorkspaceTransaction> initialize(const Project& project) override {
        ++state.initialize_calls;
        state.initialized_project = project;
        if (state.events != nullptr) {
            state.events->emplace_back("workspace.initialize");
        }
        if (throw_on_initialize) {
            throw std::runtime_error{"workspace initialization failed"};
        }
        if (return_null_transaction) {
            return nullptr;
        }
        return std::make_unique<FakeWorkspaceTransaction>(state);
    }

    WorkspaceState state;
    bool throw_on_initialize{false};
    bool return_null_transaction{false};
};

[[nodiscard]] Project make_project(std::string id, std::string root_path) {
    return Project{
        std::move(id),
        "Existing project",
        std::move(root_path),
        "2026-08-06T17:00:00Z",
        "2026-08-06T17:00:00Z",
    };
}

[[nodiscard]] bool verifies_successful_creation() {
    std::vector<std::string> events;
    FakeProjectRepository repository{&events};
    FakeIdGenerator ids{{"project-004"}, &events};
    FakeUtcClock clock{"2026-08-06T20:47:00Z", &events};
    FakePathCanonicalizer paths{"/canonical/genomics", &events};
    FakeProjectWorkspace workspace{&events};
    ProjectService service{repository, ids, clock, paths, workspace};

    const Project project = service.create(CreateProjectRequest{"Genomics", "./genomics"});
    const std::vector<std::string> expected_events{
        "path.canonicalize",
        "repository.find_by_root_path",
        "id.generate",
        "repository.find_by_id",
        "clock.now",
        "workspace.initialize",
        "repository.save",
        "workspace.commit",
    };

    return project.id() == "project-004" && project.name() == "Genomics" &&
           project.root_path() == "/canonical/genomics" &&
           project.created_at_utc() == "2026-08-06T20:47:00Z" &&
           project.updated_at_utc() == project.created_at_utc() && paths.last_input == "./genomics" &&
           repository.last_root_lookup == "/canonical/genomics" && repository.save_calls == 1U &&
           repository.saved_project.has_value() && repository.saved_project->id() == project.id() &&
           workspace.state.initialize_calls == 1U && workspace.state.commit_calls == 1U &&
           workspace.state.rollback_calls == 0U && workspace.state.initialized_project.has_value() &&
           workspace.state.initialized_project->id() == project.id() && ids.calls == 1U && clock.calls == 1U &&
           events == expected_events;
}

[[nodiscard]] bool rejects_duplicate_canonical_root_without_side_effects() {
    FakeProjectRepository repository;
    repository.projects.push_back(make_project("existing", "/canonical/shared"));
    FakeIdGenerator ids{{"unused"}};
    FakeUtcClock clock{"2026-08-06T20:47:00Z"};
    FakePathCanonicalizer paths{"/canonical/shared"};
    FakeProjectWorkspace workspace;
    ProjectService service{repository, ids, clock, paths, workspace};

    try {
        (void)service.create(CreateProjectRequest{"Duplicate", "../shared"});
    } catch (const DuplicateProjectRootError&) {
        return repository.save_calls == 0U && ids.calls == 0U && clock.calls == 0U && paths.calls == 1U &&
               workspace.state.initialize_calls == 0U;
    }
    return false;
}

[[nodiscard]] bool retries_identifier_collisions() {
    FakeProjectRepository repository;
    repository.projects.push_back(make_project("collision", "/canonical/existing"));
    FakeIdGenerator ids{{"collision", "unique-id"}};
    FakeUtcClock clock{"2026-08-06T20:47:00Z"};
    FakePathCanonicalizer paths{"/canonical/new"};
    FakeProjectWorkspace workspace;
    ProjectService service{repository, ids, clock, paths, workspace};

    const Project project = service.create(CreateProjectRequest{"New", "/new"});
    return project.id() == "unique-id" && ids.calls == 2U && clock.calls == 1U && repository.save_calls == 1U &&
           workspace.state.commit_calls == 1U;
}

[[nodiscard]] bool stops_when_path_canonicalization_fails() {
    FakeProjectRepository repository;
    FakeIdGenerator ids{{"unused"}};
    FakeUtcClock clock{"2026-08-06T20:47:00Z"};
    ThrowingPathCanonicalizer paths;
    FakeProjectWorkspace workspace;
    ProjectService service{repository, ids, clock, paths, workspace};

    try {
        (void)service.create(CreateProjectRequest{"Project", "/unavailable"});
    } catch (const std::runtime_error&) {
        return paths.calls == 1U && ids.calls == 0U && clock.calls == 0U && repository.save_calls == 0U &&
               workspace.state.initialize_calls == 0U;
    }
    return false;
}

[[nodiscard]] bool does_not_initialize_workspace_for_invalid_domain_data() {
    FakeProjectRepository repository;
    FakeIdGenerator ids{{"valid-id"}};
    FakeUtcClock clock{"2026-08-06T20:47:00Z"};
    FakePathCanonicalizer paths{"/canonical/root"};
    FakeProjectWorkspace workspace;
    ProjectService service{repository, ids, clock, paths, workspace};

    try {
        (void)service.create(CreateProjectRequest{"   ", "/root"});
    } catch (const std::invalid_argument&) {
        return repository.save_calls == 0U && workspace.state.initialize_calls == 0U;
    }
    return false;
}

[[nodiscard]] bool fails_after_identifier_attempt_limit() {
    FakeProjectRepository repository;
    repository.projects.push_back(make_project("collision", "/canonical/existing"));
    std::deque<std::string> collisions(ProjectService::maximum_id_generation_attempts, "collision");
    FakeIdGenerator ids{std::move(collisions)};
    FakeUtcClock clock{"2026-08-06T20:47:00Z"};
    FakePathCanonicalizer paths{"/canonical/new"};
    FakeProjectWorkspace workspace;
    ProjectService service{repository, ids, clock, paths, workspace};

    try {
        (void)service.create(CreateProjectRequest{"New", "/new"});
    } catch (const ProjectIdGenerationError&) {
        return ids.calls == ProjectService::maximum_id_generation_attempts && clock.calls == 0U &&
               repository.save_calls == 0U && workspace.state.initialize_calls == 0U;
    }
    return false;
}

[[nodiscard]] bool does_not_persist_when_workspace_initialization_fails() {
    FakeProjectRepository repository;
    FakeIdGenerator ids{{"project-id"}};
    FakeUtcClock clock{"2026-08-06T20:47:00Z"};
    FakePathCanonicalizer paths{"/canonical/new"};
    FakeProjectWorkspace workspace;
    workspace.throw_on_initialize = true;
    ProjectService service{repository, ids, clock, paths, workspace};

    try {
        (void)service.create(CreateProjectRequest{"New", "/new"});
    } catch (const std::runtime_error&) {
        return workspace.state.initialize_calls == 1U && workspace.state.commit_calls == 0U &&
               workspace.state.rollback_calls == 0U && repository.save_calls == 0U;
    }
    return false;
}

[[nodiscard]] bool rolls_back_workspace_when_repository_save_fails() {
    std::vector<std::string> events;
    FakeProjectRepository repository{&events};
    repository.throw_on_save = true;
    FakeIdGenerator ids{{"project-id"}, &events};
    FakeUtcClock clock{"2026-08-06T20:47:00Z", &events};
    FakePathCanonicalizer paths{"/canonical/new", &events};
    FakeProjectWorkspace workspace{&events};
    ProjectService service{repository, ids, clock, paths, workspace};

    try {
        (void)service.create(CreateProjectRequest{"New", "/new"});
    } catch (const std::runtime_error&) {
        const std::vector<std::string> expected_events{
            "path.canonicalize",
            "repository.find_by_root_path",
            "id.generate",
            "repository.find_by_id",
            "clock.now",
            "workspace.initialize",
            "repository.save",
            "workspace.rollback",
        };
        return workspace.state.initialize_calls == 1U && workspace.state.commit_calls == 0U &&
               workspace.state.rollback_calls == 1U && events == expected_events;
    }
    return false;
}

[[nodiscard]] bool rejects_null_workspace_transaction_before_persistence() {
    FakeProjectRepository repository;
    FakeIdGenerator ids{{"project-id"}};
    FakeUtcClock clock{"2026-08-06T20:47:00Z"};
    FakePathCanonicalizer paths{"/canonical/new"};
    FakeProjectWorkspace workspace;
    workspace.return_null_transaction = true;
    ProjectService service{repository, ids, clock, paths, workspace};

    try {
        (void)service.create(CreateProjectRequest{"New", "/new"});
    } catch (const std::runtime_error&) {
        return workspace.state.initialize_calls == 1U && repository.save_calls == 0U;
    }
    return false;
}

}  // namespace

int main() {
    if (!verifies_successful_creation()) {
        std::cerr << "ProjectService successful creation contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejects_duplicate_canonical_root_without_side_effects()) {
        std::cerr << "ProjectService duplicate-root contract failed\n";
        return EXIT_FAILURE;
    }
    if (!retries_identifier_collisions()) {
        std::cerr << "ProjectService identifier retry contract failed\n";
        return EXIT_FAILURE;
    }
    if (!stops_when_path_canonicalization_fails()) {
        std::cerr << "ProjectService path-failure short-circuit contract failed\n";
        return EXIT_FAILURE;
    }
    if (!does_not_initialize_workspace_for_invalid_domain_data()) {
        std::cerr << "ProjectService invalid-domain workspace contract failed\n";
        return EXIT_FAILURE;
    }
    if (!fails_after_identifier_attempt_limit()) {
        std::cerr << "ProjectService identifier exhaustion contract failed\n";
        return EXIT_FAILURE;
    }
    if (!does_not_persist_when_workspace_initialization_fails()) {
        std::cerr << "ProjectService workspace failure contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rolls_back_workspace_when_repository_save_fails()) {
        std::cerr << "ProjectService repository-failure rollback contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejects_null_workspace_transaction_before_persistence()) {
        std::cerr << "ProjectService null workspace transaction contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

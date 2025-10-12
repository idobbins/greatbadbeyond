# Repository Guidelines

## Project Structure & Module Organization
Callandor is a single Rust binary crate. The entry point lives in `src/main.rs`, and shared code should be broken into modules inside `src/` using `mod` declarations. Keep integration tests in a `tests/` folder when they grow beyond unit scenarios. Generated artifacts land in `target/`; do not commit anything under that directory.

## Build, Test, and Development Commands
- `cargo check` – quickly verify the code compiles; run after every edit (approval pre-granted) and prioritize fixing warnings or errors at the source before moving on.
- `cargo build` – compile the project and surface compile-time warnings.
- `cargo run` – build and execute the binary from the current sources.
- `cargo test` – run unit and integration tests; add `-- --nocapture` to view println output.
- `cargo fmt` – format the codebase using rustfmt; run before every commit.
- `cargo clippy` – lint with clippy’s default set; address warnings or justify them with comments.

## Dependency Management
Use `cargo add <crate>` (with `--features` as needed) to introduce new dependencies and let Cargo resolve the version, which keeps `Cargo.toml` and `Cargo.lock` synchronized. Do not edit `Cargo.toml` manually for additions that the `cargo add` workflow supports; rely on direct edits only for metadata tweaks or settings unavailable through the command.

## Coding Style & Naming Conventions
Follow the default Rust style (rustfmt 4-space indentation, trailing commas where available). Name modules and functions in `snake_case`, types and enums in `PascalCase`, and constants in `SCREAMING_SNAKE_CASE`. Prefer explicit `use` statements and keep `main` focused; move logic into smaller modules for clarity. Document public APIs with `///` doc comments when behavior is non-obvious.

## Testing Guidelines
Use Rust’s built-in test framework. Place focused unit tests in the same file under a `#[cfg(test)] mod tests` block, and integration tests in separate files under `tests/`. Write tests alongside new features and ensure they handle both happy-path and error scenarios. Aim to cover every public function that manipulates state. Use descriptive test names like `handles_empty_input` to highlight intent.
Additionally, smoke test the binary with a timeout to surface runtime issues early—for example, `timeout 60 cargo run` to explore the happy path without risking a hung process.

## Documentation & Research
When adopting new crates or APIs, pull in documentation through the Context7 tool to ensure the latest guidance (`context7 --resolve <library>` then `context7 --docs`). Capture key findings in code comments or PR notes if they influence design decisions, and favor official docs over blog posts.

## Commit & Pull Request Guidelines
Write commit messages in the imperative mood (`Add parsing module`) with a short summary (<72 chars) followed by optional context in the body. Reference related issues as `Fixes #123` when applicable. Pull requests should describe the motivation, the key changes, and any follow-up tasks. Include validation evidence (`cargo check`, `cargo test`) or manual notes, and attach screenshots when altering user-facing behavior.

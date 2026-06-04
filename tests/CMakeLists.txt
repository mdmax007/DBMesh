# GoogleTest is fetched in cmake/Dependencies.cmake and available here.

# ── Unit tests ──────────────────────────────────────────────────────────
add_subdirectory(unit)

# ── Integration tests (require docker-compose up first) ─────────────────
# add_subdirectory(integration)

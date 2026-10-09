# Security policy

## Supported versions

TerrainGen is pre-1.0: fixes go into the latest release and `main`.

## Reporting a vulnerability

Please do not open a public issue for a security problem. Report it privately through GitHub's
[private vulnerability reporting](https://github.com/alexmaietta/terraingen/security/advisories/new)
with a description, the affected version and, if you can, a file or configuration that shows it.

TerrainGen reads untrusted input in a few places: configuration files (JSON), GeoTIFF rasters,
and crater, rock and albedo tables. Crashes, hangs or memory errors from malformed inputs there are
in scope. You should hear back within a week; once a fix is released the advisory will be published
with credit to you, unless you prefer otherwise.

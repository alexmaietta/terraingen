# Command line

The `terraingen` command generates the terrain a JSON configuration describes and writes its heightmaps, albedo, crater and boulder lists, and terrain and boulder meshes. The Python package installs it, as do the releases. The [Quickstart](../../getting-started/quickstart.md) runs a first configuration, and the [configuration reference](../../reference/index.md) lists every setting.

## Usage

This is what `terraingen --help` prints.

{{ cli_help() }}

## Examples

Generate a terrain, writing its outputs beside the configuration.

```sh
terraingen config.json
```

Check a configuration without generating, then generate it on a GPU into another folder.

```sh
terraingen validate config.json
terraingen config.json --backend gpu --output-dir out/
```

Point an editor at the configuration schema, for completion and checking as you type.

```sh
terraingen schema > terraingen.schema.json
```

# P10-Graphs-n-Shit-Silver-Potato-Edition

Master thesis code by group 10-5 on AAU Software Engineer 2026.

# Tables
We run ETL directly on a PostgreSQL instance. Each ETL run will create a single graph with a configuration done via `.info.yaml`
ETL from relational GIS data to SAE and SAN needs the following tables:

| experiment.data_for_graph  |||||||
|-------|-----|------------|-|-|-|-|
| segmentkey (int)| startpoint (int) | endpoint (int) | category (string) | direction (string) | segmentgeo (geostring) | name (string) |

| regions.dk_municipalities  ||||||
|-------|-----|------------|-|-|-|
|dk_municipalitykey (int) | code (int) | region_code (int) | name (string) | region_name (string) | geog (geostring) |

| mapmatched_data.viterbi_match_osm_dk_20140101  |||||||
|-------|-----|------------|-|-|-|-|
|trip_id (int)| segmentkey (int) |  meters_driven (real) |  seconds (real) |

## Configuration
Configuration is done via `.info.yaml`

We expect the following fields:
```yaml
server: xxx.xxx.xxx.xxx
port: POSTGRES_PORT
database-user: USERNAME
password: PASSWORD
database-name: DB_NAME
PostgreSQL-Version: PG_VERSION
graph_prefix: SAE_ or SAN_
```

# Creating the graphs
In `main.cpp` there are a number of method calls on the `db` instance.
Uncomment the ones needed depending on the graph you want to create. Keep in mind that 
```cpp
db.ResetGraph();
db.CreateMunicipalities();
```
Should always be called.

**IMPORTANT:** Before running the compiled program, make sure you have a file call `.info.yaml` in the folder you are executing from.
#include<cassert>
#include <yaml-cpp/yaml.h>
#include <libpq-fe.h>
#include "DBfunctions.hpp"

#define assertm(exp, msg) assert((void(msg), exp))

int main(){
    YAML::Node config = YAML::LoadFile(".info.yaml");
    const std::string host = config["server"].as<std::string>();
    const std::string port = config["port"].as<std::string>();
    const std::string dbname = config["database-name"].as<std::string>();
    const std::string username = config["database-user"].as<std::string>();
    const std::string password = config["password"].as<std::string>();
    const std::string graph_prefix = config["graph_prefix"].as<std::string>();
    const std::string graph_name = graph_prefix + "segment_as_nodes_graph";
    
    DBfunctions db = DBfunctions(host, port, dbname, username, password,graph_prefix,graph_name);
    db_result_t row = returnResult(db.conn_, "select * from experiments.data_for_graph where segmentkey = 23781;");

    bool SAE_fast = true;
    bool SAN_fast = true;
    u_int64_t iteration = 0; 
    std::string graph_type = "PostGIS_";
    
    
    
    for(int i = 0; i<5;i++)
    {
              
    
    db.BenchmarkQuery(graph_type + "fetch_segment","SELECT * FROM experiments.data_for_graph;");
            
    db.BenchmarkQuery(graph_type + "fetch_property", "SELECT * FROM experiments.data_for_graph where name = 'Vesterbro';"); 


    db.BenchmarkQuery(graph_type + "fetch_node_with_relations_in",
    "SELECT d2.*"
    "FROM experiments.data_for_graph d1 "
    "JOIN experiments.data_for_graph d2 "
    "ON d1.startpoint = d2.endpoint or d1.endpoint = d2.endpoint "
    "WHERE d1.segmentkey = 413648;"

    );

    db.BenchmarkQuery(graph_type +"fetch_node_with_relations_out", 
    "SELECT d2.* "
    "FROM experiments.data_for_graph d1 "
    "JOIN experiments.data_for_graph d2 "
    "ON d1.endpoint = d2.startpoint or d1.startpoint = d2.startpoint "
    "WHERE d1.segmentkey = 413648;"

    );

    db.BenchmarkQuery(graph_type +"fetch_specific_segment_in_and_out",
    "SELECT d2.* "
    "FROM experiments.data_for_graph d1 "
    "JOIN experiments.data_for_graph d2 "
    "ON d1.startpoint = d2.endpoint or d1.endpoint = d2.endpoint or d1.endpoint = d2.startpoint or d1.startpoint = d2.startpoint "
    "WHERE d1.segmentkey = 413648;"

    );

    db.BenchmarkQuery(graph_type +"node_degree", 
    "SELECT count(d2.*) "
    "FROM experiments.data_for_graph d1 "
    "JOIN experiments.data_for_graph d2 "
    "ON d1.startpoint = d2.endpoint or d1.endpoint = d2.endpoint or d1.endpoint = d2.startpoint or d1.startpoint = d2.startpoint "
    "WHERE d1.segmentkey = 413648;"

    );

    db.BenchmarkQuery(graph_type +"create_specific_shortcut", 
    "insert into experiments.shortcut values (1, 24774, 359481, 'dummy edge');"
    );

        db.BenchmarkQuery(graph_type + "set_shortcut_property", 
        "UPDATE experiments.shortcut "
        "SET name = 'this is quick' "
        "WHERE id = 1;"
        
    );

    db.BenchmarkQuery(graph_type + "nullify_shortcut_property", 
    "UPDATE experiments.shortcut "
    "SET name = null "
    "WHERE id = 1;"
    );

    db.BenchmarkQuery(graph_type + "delete_shortcuts",  
    "DELETE * FROM experiments.shortcut;"
        );
        
    db.BenchmarkQuery(graph_type + "fetch_segments_which_are_in_multiple_sub_municipalities", 
        "SELECT s.segmentkey, s.segmentgeo "
        "FROM experiments.data_for_graph s "
            "JOIN regions.dk_municipalities r ON ST_Intersects(r.geog, s.segmentgeo) "
            "GROUP BY s.segmentkey, s.segmentgeo "
            "HAVING COUNT(r.dk_municipalitykey) = 1;"
        );
        
        db.BenchmarkQuery(graph_type + "fetch_by_area", "SELECT *"
        "FROM experiments.data_for_graph "
            "WHERE ST_DWithin( "
            "segmentgeo::geography, "
            "ST_SetSRID(ST_MakePoint(9.984202634948444, 57.01489256594891), 4326)::geography, "
            "  1000 "
            ");"
            );
            
            db.BenchmarkQuery(graph_type + "KNN", 
            "SELECT *, "
            "ST_Distance(center.segmentgeo::geography, s.segmentgeo::geography) AS distance_m "
            "FROM experiments.data_for_graph s "
            "JOIN experiments.data_for_graph center ON center.segmentkey = 420684 "
            "WHERE ST_DWithin(center.segmentgeo::geography, s.segmentgeo::geography, 500) "
            "AND s.segmentkey <> center.segmentkey "
            "ORDER BY distance_m ASC limit 1;"
        );
        
    PQexec(db.conn_,"delete from experiments.data_for_graph where segmentkey = 23781;");

    db.BenchmarkQuery(graph_type + "insert_segment_23781", 
        std::format("insert into experiments.data_for_graph values ("
            "{},"                                 //segmentkey
            "{},"                                 //startpoint
            "{},"                                 //endpoint 
            "'{}',"                                 //minutes
            "{},"                                 //meters
            "'{}'::functionality.osm_categories," //category
            "'{}'::functionality.direction_map,"  //direction
            "{},"                                 //segangle
            "{},"                                 //categoryid
            "{},"                                 //speedlimit_forward
            "{},"                                 //speedlimit_backward
            "{},"                                 //segmentid
            "{},"                                 //logprime
            "ST_GeogFromWKB(decode('{}','hex')),"  //segmentgeo
            "'{}'"                                  //name
            ");",
        row[0][0],
        row[0][1],
        row[0][2],  
        NULL,
        row[0][4],
        row[0][5],
        row[0][6],
        row[0][7],
        row[0][8],
        row[0][9],
        row[0][10],
        row[0][11],
        row[0][12],
        row[0][13],
        row[0][14])
    );
}

return 0; 
}
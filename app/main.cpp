#include <iostream>
#include <format>
#include <libpq-fe.h>
#include <yaml-cpp/yaml.h>
#include "DBfunctions.hpp"

//query used in age viewer to show nodes and edges. Should use limit for speedup.
// select * from cypher('dummy_graph',$$ match(n)-[r]->(m) return n,r,m limit 100 $$) as (n agtype, r agtype, m agtype);
// select * from cypher('dummy_graph',$$ match(n) detach delete n $$) as (n agtype); -> deletes all data in the graph.
int main() {
    
    YAML::Node config = YAML::LoadFile(".info.yaml");
    
    const std::string host = config["server"].as<std::string>();
    const std::string port = config["port"].as<std::string>();
    const std::string dbname = config["database-name"].as<std::string>();
    const std::string username = config["database-user"].as<std::string>();
    const std::string password = config["password"].as<std::string>();
    const std::string graph_prefix = config["graph_prefix"].as<std::string>();
    const std::string graph_name = graph_prefix + "segment_as_nodes_graph";

    DBfunctions db = DBfunctions(host, port, dbname, username, password, graph_prefix, graph_name);
    
    auto start_timer2{std::chrono::steady_clock::now()};
    
    //Always needed
    //db.ResetGraph();
    //db.CreateMunicipalities();
    
    //Only for SAE
    // db.CreateNodesForAllStartAndEndpoints();
    // db.CreateEdgesForAllIntersections();
    // db.CreateTrajectoryConnectionForSAE();

    // auto query1{std::chrono::steady_clock::now()};
    // std::chrono::duration<double> time_elapsed1{query1 - start_timer2};
    // std::cout << "Making SAE graph took: " << time_elapsed1.count() << " seconds" << std::endl;
    
    //Only for SAN
    //db.CreateNodesForAllSubMunicipalities();
    db.CreateEdgesForAllSegments();
    db.CreateTrajectories();

    auto query1{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed1{query1 - start_timer2};
    std::cout << "Making SAN graph took: " << time_elapsed1.count() << " seconds" << std::endl;
/*
    std::vector<std::vector<std::string>> data = extract_Data_From_CSV("/home/rasmusbertelsennoerfjand/Documents/Aalborg universitet/10. Semester/DATA/Alle_segmenter_i_Aalborg_kommune.csv");
    
    //query used to test if it is possible to return data from apache age.
    const char* query = "select * from cypher('dummy_graph',$$ match(n)-[r]->(m) return n,r,m $$) as (n agtype, r agtype, m agtype);";
    
    //returnResult is from the library in src/DBfunctions.cpp
    std::vector<std::vector<std::string>> test = returnResult(conn,query);
    
    //inserts all segments as nodes into apache age.
    //insert_segments_as_nodes(conn,data);
    
    //add_edges(conn, data);
    
    //std::vector<std::vector<std::string>> testForPrint =extract_Data_From_CSV("/home/rasmusbertelsennoerfjand/Documents/Aalborg universitet/10. Semester/DATA/Alle_segmenter_i_Aalborg_kommune.csv");
    debug_print(test);
    
    //Closes connection to DB
    PQfinish(conn);
    */
    return 0;
}
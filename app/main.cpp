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


    //Making connection.
    const std::string conninfo = "host=" + host + " port=" + port + " dbname=" + dbname + " user=" + username + " password=" + password;
    PGconn* conn = PQconnectdb(conninfo.c_str());
    
    // checks for connection to DB if not, gives and error.
    switch (PQstatus(conn))
    {
    case CONNECTION_OK:
        std::cout << "Connection to postgres succesful" << std::endl; 
        break;
    default:
        std::cerr << "Connection failed, maybe check if Docker container is running" << std::endl;

        break;
    }
        
    db_result_t municipalities = returnResult(conn, "SELECT * FROM regions.dk_municipalities ORDER BY dk_municipalitykey");

    std::string current_city;
    std::string current_region;
    uint8_t sub_municipality_count = 1;
    std::cout << municipalities.size() << std::endl;
    
    for (const auto& sub_municipality : municipalities) {

        if (current_region.compare(sub_municipality.at(2))) {
            //std::cout << current_region << " is not " << sub_municipality.at(2) << std::endl;
            current_region = sub_municipality.at(2);
            std::cout << std::format("SELECT * FROM cypher('nicklas_dummy_graph', $$ CREATE (:region {{name: '{}', region_code: '{}'}}) $$) as (n agtype)", sub_municipality.at(4), sub_municipality.at(2)) << std::endl;
            
        }
        if (current_city.compare(sub_municipality.at(1))) {
            sub_municipality_count = 1;
            //std::cout << current_city << " is not " << sub_municipality.at(1) << std::endl;
            current_city = sub_municipality.at(1);
            std::cout << std::format("SELECT * FROM cypher('nicklas_dummy_graph', $$ CREATE (:municipality {{name: '{}', code: '{}'}}) $$) as (n agtype)", sub_municipality.at(3), sub_municipality.at(1)) << std::endl;
    
        }
        std::cout << std::format("SELECT * FROM cypher('nicklas_dummy_graph', $$ CREATE (:sub_municipality {{name: '{}-{}', dk_municipalitykey: '{}'}}) $$) as (n agtype)", sub_municipality_count, sub_municipality.at(3), sub_municipality.at(0)) << std::endl;
        sub_municipality_count += 1;
        
    }

    /*
    //Declares search path for apache age
    PGresult* res = PQexec(conn,"set search_path = ag_catalog, \"$user\", public;");   
    
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
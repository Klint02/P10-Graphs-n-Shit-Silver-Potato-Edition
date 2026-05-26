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
  
    
    DBfunctions db = DBfunctions(host, port, dbname, username, password,"production_","production_segment_as_nodes_graph");
    DBfunctions db1 = DBfunctions(host, port, dbname, username, password, "rasmus_SAE_", "rasmus_SAE_segment_as_nodes_graph");
    db.BenchmarkQuery("preload_SAN", "SELECT * FROM cypher('production_segment_as_nodes_graph', $$ MATCH (s:segment) RETURN s limit 1 $$) as (s agtype); ");
    db.BenchmarkQuery("preload_SAE", "SELECT * FROM cypher('rasmus_SAE_segment_as_nodes_graph', $$ MATCH (s:intersection) RETURN s limit 1 $$) as (s agtype); ");


    db_result_t row = returnResult(db.conn_, "select * from experiments.data_for_graph where segmentkey = 23781;");

    bool SAE_fast = true;
    bool SAN_fast = true;
    u_int64_t iteration = 0; 
    std::string graph_rel = "PostGIS_";
    std::string graph_SAN = "SAN_";
    std::string graph_SAE = "SAE_";
    
    
    
    for(int i = 0; i<5;i++)
    {
              
    //----------------------------------Q1: fetch_segments-----------------------------------------
    //postgis
    db.BenchmarkQuery(graph_rel + "fetch_segment","SELECT segmentkey FROM experiments.data_for_graph;");
    
    //SAN        
     db.BenchmarkQuery(graph_SAN + "fetch_segments_SAN", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (s:segment) "
        "RETURN s.segmentkey "
        "$$) as (s agtype); "
        , db.graph_name_)

    ); 

    //SAE
    db1.BenchmarkQuery(graph_SAE + "fetch_segments_SAE", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:segment]->() "
        "RETURN distinct s.segmentkey "
        "$$) as (s agtype); "
        , db1.graph_name_)

    );

    //----------------------------------Q2: fetch_by_property-----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel + "fetch_property", "SELECT segmentkey FROM experiments.data_for_graph where name = 'Vesterbro';"); 

    //SAN
    db.BenchmarkQuery(graph_SAN + "fetch_property", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(s:segment {{name: 'Vesterbro'}}) "
        "return s.segmentkey "
        "$$) as (Node1 agtype); "
        , db.graph_name_)
    );

    //SAE
    db1.BenchmarkQuery(graph_SAE + "fetch_property", std::format(
        "select * from cypher('{}', $$ "
        "MATCH()-[s:segment {{name: 'Vesterbro'}}]->() "
        "return distinct s.segmentkey "
        "$$) as (Node1 agtype); "
        , db1.graph_name_)
    );

    //----------------------------------Q3: fetch_node_with_relations_in-----------------------------------------
    //postgis returner 2 row mindre fordi 413649 har direction both 
    //Postgis
    db.BenchmarkQuery(graph_rel + "fetch_node_with_relations_in",
    "SELECT d2.*"
    "FROM experiments.data_for_graph d1 "
    "JOIN experiments.data_for_graph d2 "
    "ON d1.startpoint = d2.endpoint or d1.endpoint = d2.endpoint "
    "WHERE d1.segmentkey = 413648;"

    );

    //SAN
      db.BenchmarkQuery(graph_SAN + "fetch_node_with_relations_in", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A:segment {{segmentkey: '23781'}})<-[R]-(B:segment) "
        "return B.segmentkey "
        "$$) as (Node1 agtype); "
        , db.graph_name_)

    );

    //SAE
    db1.BenchmarkQuery(graph_SAE + "fetch_node_with_relations_in", std::format(
        "SELECT * FROM cypher('rasmus_SAE_segment_as_nodes_graph', $$ "
        "MATCH (start)-[r:segment {{segmentkey : '413648'}}]->(ender:intersection) "
        "MATCH (other)-[r2:segment]->(ender)  "
        "WHERE r2.segmentkey <> r.segmentkey "
        "RETURN r2.segmentkey  "
        "$$) AS (segmentkey agtype); "
        , db1.graph_name_)

    );

    //----------------------------------Q4: fetch_node_with_relations_out-----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel +"fetch_node_with_relations_out", 
    "SELECT d2.* "
    "FROM experiments.data_for_graph d1 "
    "JOIN experiments.data_for_graph d2 "
    "ON d1.endpoint = d2.startpoint or d1.startpoint = d2.startpoint "
    "WHERE d1.segmentkey = 413648;"
    );

     //SAN
      db.BenchmarkQuery(graph_SAN + "fetch_node_with_relations_in", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A:segment {{segmentkey: '23781'}})-[R]->(B:segment) "
        "return B.segmentkey "
        "$$) as (Node1 agtype); "
        , db.graph_name_)

    );

    //SAE
    db1.BenchmarkQuery(graph_SAE + "fetch_node_with_relations_in", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (start)-[r:segment {{segmentkey : '413648'}}]->(ender:intersection) "
        "MATCH (other)-[r2:segment]->(ender)  "
        "WHERE r2.segmentkey <> r.segmentkey "
        "RETURN r2.segmentkey  "
        "$$) AS (segmentkey agtype); "
        , db1.graph_name_)

    );

    //----------------------------------Q5: fetch_node_with_relations_in_and_out-----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel +"fetch_specific_segment_in_and_out",
    "SELECT d2.* "
    "FROM experiments.data_for_graph d1 "
    "JOIN experiments.data_for_graph d2 "
    "ON d1.startpoint = d2.endpoint or d1.endpoint = d2.endpoint or d1.endpoint = d2.startpoint or d1.startpoint = d2.startpoint "
    "WHERE d1.segmentkey = 413648;"

    );

    //SAN
    db.BenchmarkQuery(graph_SAN + "fetch_node_with_relations_in", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A:segment {{segmentkey: '23781'}})-[R]-(B:segment) "
        "return B.segmentkey "
        "$$) as (Node1 agtype); "
        , db.graph_name_)

    );

    //SAE
    db1.BenchmarkQuery(graph_SAE + "fetch_node_with_relations_in", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (a:intersection {{point:24775}})-[r:segment]-(b) "
        "RETURN r2.segmentkey  "
        "$$) AS (segmentkey agtype); "
        , db1.graph_name_)

    );

    //----------------------------------Q6: node_degree-----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel +"node_degree", 
    "SELECT count(d2.*) "
    "FROM experiments.data_for_graph d1 "
    "JOIN experiments.data_for_graph d2 "
    "ON d1.startpoint = d2.endpoint or d1.endpoint = d2.endpoint or d1.endpoint = d2.startpoint or d1.startpoint = d2.startpoint "
    "WHERE d1.segmentkey = 413648;"
    );

    
    //SAN
    db.BenchmarkQuery(graph_SAN + "node_degree", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (n {{segmentkey:'413648'}})-[r]-(b)  "
        "RETURN count(r) "
        "$$ ) AS (segmentkey agtype); ", db.graph_name_)
    );

    //SAE
    db1.BenchmarkQuery(graph_SAE + "node_degree", std::format(
    "SELECT * FROM cypher('{}', $$ "
    "MATCH (n {{point:'24775'}})-[r]-(b) "
    "RETURN count(r) "
    "$$ ) AS (segmentkey agtype); ", db1.graph_name_)
    );

        //----------------------------------Q7: create_shortcut -----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel +"create_specific_shortcut", 
    "insert into experiments.shortcut values (1, 24774, 359481, 'dummy edge');"
    );

    db.BenchmarkQuery(graph_SAN + "create_shortcut", std::format(
    "SELECT * FROM cypher('{}', $$ "
    "match (n {{segmentkey:'418703'}}),(b {{segmentkey: '418712'}})  "
    "CREATE (n)-[r:shortcut]->(b) "
    "$$ ) AS (segmentkey agtype); ", db.graph_name_)
    );

    db1.BenchmarkQuery(graph_SAE + "create_shortcut", std::format(
    "SELECT * FROM cypher('{}', $$ "
    "MATCH (n {{point:'24775'}}), (b: {{point:'399233'}}) "
    "create (n)-[r:shortcut]->(b) "
    "$$ ) AS (segmentkey agtype); ", db1.graph_name_)
    );

    //----------------------------------Q8: update_property -----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel + "set_shortcut_property", 
    "UPDATE experiments.shortcut "
    "SET name = 'this is quick' "
    "WHERE id = 1;"

    );

    //SAN
    db.BenchmarkQuery(graph_SAE + "node_degree", std::format(
    "SELECT * FROM cypher('{}', $$ "
    "MATCH ()-[s:shortcut]->() "
    "SET s.name = \"This is a description\" "
    "$$ ) AS (s agtype); ", db.graph_name_)
    );

    //SAE
    db1.BenchmarkQuery(graph_SAE + "node_degree", std::format(
    "SELECT * FROM cypher('{}', $$ "
    "MATCH ()-[r:shortcut]->() "
    "SET s.name = \"This is a description\" "
    "$$ ) AS (s agtype); ", db1.graph_name_)
    );


    //----------------------------------Q9: Update_property to null -----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel + "Update_property", 
    "UPDATE experiments.shortcut "
    "SET name = null "
    "WHERE id = 1;"
    );

    //SAN
    db.BenchmarkQuery(graph_SAE + "Update_property", std::format(
    "SELECT * FROM cypher('{}', $$ "
    "MATCH ()-[r:shortcut]->() "
    "SET s.name = null "
    "$$ ) AS (s agtype); ", db.graph_name_)
    );

    //SAE
    db1.BenchmarkQuery(graph_SAE + "Update_property", std::format(
    "SELECT * FROM cypher('{}', $$ "
    "MATCH ()-[r:shortcut]->() "
    "SET s.name = null "
    "$$ ) AS (s agtype); ", db1.graph_name_)
    );

    //----------------------------------Q10: delete_shortcuts -----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel + "delete_shortcut",  
    "DELETE * FROM experiments.shortcut;"
    );

    //SAN
    db.BenchmarkQuery(graph_SAN + "delete_shortcuts", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[r:shortcut]->() "
        "delete r "
        "$$ ) AS (s agtype); ", db.graph_name_)
        );

    //SAE
    db1.BenchmarkQuery(graph_SAE + "delete_shortcuts", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[r:shortcut]->() "
        " delete r "
        "$$ ) AS (s agtype); ", db1.graph_name_)
        );

    
    //----------------------------------Q11: fetch_segments_which_are_in_multiple_sub_municipalities -----------------------------------------
    //Postgis    
    db.BenchmarkQuery(graph_rel + "fetch_segments_which_are_in_multiple_sub_municipalities", 
        "SELECT s.segmentkey, s.segmentgeo "
        "FROM experiments.data_for_graph s "
            "JOIN regions.dk_municipalities r ON ST_Intersects(r.geog, s.segmentgeo) "
            "GROUP BY s.segmentkey, s.segmentgeo "
            "HAVING COUNT(r.dk_municipalitykey) = 1;"
        );
    
    
    //SAN
     db.BenchmarkQuery(graph_SAE + "fetch_segments_which_are_in_multiple_sub_municipalities", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[r:contains]->(b:segment) "
        "with count(r) as counter, b "
        "where counter > 1 "
        "return b.segmentkey "
        "$$ ) AS (s agtype); ", db.graph_name_)
        );

    //SAE
     db1.BenchmarkQuery(graph_SAE + "fetch_segments_which_are_in_multiple_sub_municipalities", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[r:shortcut]->() "
        " delete r "
        "$$ ) AS (s agtype); ", db1.graph_name_)
        );

    
    //----------------------------------Q12: fetch_by_area-----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel + "fetch_by_area", "SELECT * "
    "FROM experiments.data_for_graph "
        "WHERE ST_DWithin( "
        "segmentgeo::geography, "
        "ST_SetSRID(ST_MakePoint(9.984202634948444, 57.01489256594891), 4326)::geography, "
        "  1000 "
        ");"
        );

    //SAN ?
    
    //SAE ?
       
    //----------------------------------Q13: KNN-----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel + "KNN", 
    "SELECT segmentkey, "
    "ST_Distance(center.segmentgeo::geography, s.segmentgeo::geography) AS distance_m "
    "FROM experiments.data_for_graph s "
    "JOIN experiments.data_for_graph center ON center.segmentkey = 420684 "
    "WHERE ST_DWithin(center.segmentgeo::geography, s.segmentgeo::geography, 500) "
    "AND s.segmentkey <> center.segmentkey "
    "ORDER BY distance_m ASC limit 3;"
        );
    
    //SAN
    db.BenchmarkQuery(graph_SAN + "KNN", std::format(
            "SELECT * from cypher('{}', $$ "
            "MATCH (A:segment {{segmentkey: '617393'}})-[R*1..3]->(B) "
            "return B.segmentkey "
            "$$) as (segmentkey agtype); "
            , db.graph_name_)
            
        );

    //SAE
    db1.BenchmarkQuery(graph_SAE + "KNN", std::format(
            "SELECT * from cypher('{}', $$ "
            "MATCH (A:intersection {point:24775})-[R:segment*1..3]->(B) "
            "return B.segmentkey "
            "$$) as (segmentkey agtype); "
            , db1.graph_name_)
            
        ); 
    
    //delete row where segmentkey=23781
    PQexec(db.conn_,"delete from experiments.data_for_graph where segmentkey = 23781;");
    
    //----------------------------------Q14: create_segment_23781 -----------------------------------------
    //Postgis
    db.BenchmarkQuery(graph_rel + "insert_segment_23781", 
        std::format("insert into experiments.data_for_graph values ("
            "{},"                                 //segmentkey
            "{},"                                 //startpoint
            "{},"                                 //endpoint 
            "'{}',"                               //minutes
            "{},"                                 //meters
            "'{}'::functionality.osm_categories," //category
            "'{}'::functionality.direction_map,"  //direction
            "{},"                                 //segangle
            "{},"                                 //categoryid
            "{},"                                 //speedlimit_forward
            "{},"                                 //speedlimit_backward
            "{},"                                 //segmentid
            "{},"                                 //logprime
            "ST_GeogFromWKB(decode('{}','hex'))," //segmentgeo
            "'{}'"                                //name
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
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

    DBfunctions db = DBfunctions(host, port, dbname, username, password, graph_prefix, graph_name);
    DBfunctions db1 = DBfunctions(host, port, dbname, username, password, "rasmus_SAE_", "segment_as_nodes_graph");
    db.BenchmarkQuery("preload", "SELECT * FROM cypher('production_segment_as_nodes_graph', $$ MATCH (s:segment) RETURN s limit 1 $$) as (s agtype); ");

    std::string graph_type = "SAN_";
    
    db.BenchmarkQuery(graph_type + "fetch_all_connected_to", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[c:connected_to]->() "
        "RETURN c "
        "$$) as (c agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "fetch_graph", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A)-[R]->(B) "
        "return A,R,B "
        "$$) as (Node1 agtype, relation agtype, Node2 agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "fetch_segments_SAN", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (s:segment) "
        "RETURN s.segmentkey "
        "$$) as (s agtype); "
        , db.graph_name_)

    ); 
    
  
    
    db.BenchmarkQuery(graph_type + "fetch_property", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(s:segment {{name: 'Vesterbro'}}) "
        "return s "
        "$$) as (Node1 agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery(graph_type + "fetch_node_with_relations_out", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A:segment {{segmentkey:23781}})-[R]->(B) "
        "return A,R,B "
        "$$) as (Node1 agtype, relation agtype, Node2 agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "fetch_node_with_relations_in", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A:segment {{segmentkey:23781}})<-[R]-(B) "
        "return A,R,B "
        "$$) as (Node1 agtype, relation agtype, Node2 agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "fetch_specific_segment_in_and_out", std::format(
        "select * from cypher('{}',$$ "
        "match(a:segment {{segmentkey: 23781}})-[b]-(c) "
        "return a,b,c"
        "$$) as (a agtype,b agtype,c agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery(graph_type + "node_degree", std::format(
        "select * from cypher('{}',$$ "
        "match(a:segment {{segmentkey: 23781}})-[b]-(c) "
        "return count(b)"
        "$$) as (b agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "create_specific_shortcut", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (a:segment{{segmentkey:23781}}),(b:segment{{segmentkey:1}})  "
        "CREATE (a)-[s:shortcut]->(b)  "
        "$$) AS (s agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery(graph_type + "set_shortcut_property", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:shortcut]->() "
        "SET s.lightning_route = \"This is quick\" "
        "$$) as (s agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "nullify_shortcut_property", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:shortcut]->() "
        "SET s.lightning_route = NULL "
        "$$) as (s agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "delete_shortcuts", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:shortcut]->() "
        "DELETE s "
        "$$) as (s agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "create_dummy_nodes", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (s:segment) "
        "CREATE (d:dummy_node {{name:s.name}}) "
        "$$) as (d agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery(graph_type + "delete_dummy_nodes", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (d:dummy_node) "
        "DETACH DELETE d "
        "$$) as (d agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery(graph_type + "fetch_neighbourhood", std::format(
        "SELECT * from cypher('{}', $$ "
        "MATCH p = (A:segment {{segmentkey:617393}})-[R*1..N]->(B:segment {{segmentkey:617394}}) "
        "return p "
        "order by length(p) asc "
        "limit 1 "
        "$$) as (path agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery(graph_type + "fetch_bridge",  std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (s:segment)-[c:connected_to]-() "
        "WITH s, count(c) as NeighbourCount "
        "WHERE NeighbourCount=1 "
        "RETURN s "
        "$$) as (s agtype); "
        , db.graph_name_)

    );

    graph_type = "SAE";
    //-------------------------------------- SAE BEGINS HERE --------------------------------------
    db.BenchmarkQuery(graph_type + "fetch_all_connected_to", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (p:intersection) "
        "RETURN p "
        "$$) as (p agtype); "
        , db.graph_name_)

    );
    

    db.BenchmarkQuery(graph_type + "fetch_graph", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A)-[R]->(B) "
        "return A,R,B "
        "$$) as (Node1 agtype, relation agtype, Node2 agtype); "
        , db.graph_name_)

    );
     
    db.BenchmarkQuery(graph_type + "fetch_segments", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:segment]->() "
        "RETURN distinct s.segmentkey "
        "$$) as (s agtype); "
        , db.graph_name_)

    );
  
    
    db.BenchmarkQuery(graph_type + "fetch_property", std::format(
        "select * from cypher('{}', $$ "
        "MATCH()-[s:segment {{name: 'Vesterbro'}}]->() "
        "WITH s.segmentkey AS key, head(collect(s)) AS s0 "
        "return s0 "
        "$$) as (Node1 agtype); "
        , db.graph_name_)

    );

    
    db.BenchmarkQuery(graph_type + "fetch_node_with_relations_out", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A:intersection{{point:22501}})-[R]->(B) "
        "return A,R,B "
        "$$) as (Node1 agtype, relation agtype, Node2 agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "fetch_node_with_relations_in", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A:intersection{{point:22501}})<-[R]-(B) "
        "return A,R,B "
        "$$) as (Node1 agtype, relation agtype, Node2 agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "fetch_specific_segment_in_and_out", std::format(
        "select * from cypher('{}',$$ "
        "match(a:intersection{{point:22501}})-[b]-(c) "
        "return a,b,c "
        "$$) as (a agtype,b agtype,c agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "fetch_node_degree", std::format(
        "select * from cypher('{}',$$ "
        "match(a:interseciton {{point: 22501}})-[b]-(c) "
        "return count(b) "
        "$$) as (a agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "create_specific_shortcut", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (a:intersection{{point:22501}}),(b:intersection{{point:1}})  "
        "CREATE (a)-[s:shortcut]->(b) "
        "$$) AS (s agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery(graph_type + "set_shortcut_property", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:shortcut]->() "
        "SET s.lightning_route = \"This is quick\" "
        "$$) as (s agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery(graph_type + "nullify_shortcut_property", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:shortcut]->() "
        "SET s.lightning_route = NULL "
        "$$) as (s agtype); "
        , db.graph_name_)

    );
    
    
    db.BenchmarkQuery(graph_type + "delete_shortcuts", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:shortcut]->() "
        "DELETE s "
        "$$) as (s agtype); "
        , db.graph_name_)
        
    );
    
    db.BenchmarkQuery(graph_type + "create_dummy_nodes", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (i:intersection) "
        "CREATE (d:dummy_node {{point:i.point}}) "
        "$$) as (d agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery(graph_type + "delete_dummy_nodes", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (d:dummy_node) "
        "DETACH DELETE d "
        "$$) as (d agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery(graph_type + "fetch_neighbourhood", std::format(
        "SELECT * from cypher('{}', $$ "
        "MATCH p = (A:intersection {{point:1}})-[R*1..10]->(B:intersection {{point:5}}) "
        "return p "
        "limit 1 "
        "$$) as (path agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery(graph_type + "fetch_bridge",  std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (i:intersection)-[s:segment]-() "
        "WITH i, count(s) as NeighbourCount "
        "WHERE NeighbourCount=1 "
        "RETURN i "
        "$$) as (i agtype); "
        , db.graph_name_)

    );


    return 0; 
}
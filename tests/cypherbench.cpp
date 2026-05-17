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
    db.BenchmarkQuery("preload", "SELECT * FROM cypher('production_segment_as_nodes_graph', $$ MATCH (s:segment) RETURN s limit 1 $$) as (s agtype); ");

    
    
    db.BenchmarkQuery("fetch_all_connected_to", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[c:connected_to]->() "
        "RETURN c "
        "$$) as (c agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery("fetch_graph", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A)-[R]->(B) "
        "return A,R,B "
        "$$) as (Node1 agtype, relation agtype, Node2 agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery("fetch_segments", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (s:segment) "
        "RETURN s.name "
        "$$) as (s agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery("fetch_node_with_relations_1", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A {{id:1234}})-[R]->(B) "
        "return A,R,B "
        "$$) as (Node1 agtype, relation agtype, Node2 agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery("fetch_node_with_relations_1", std::format(
        "select * from cypher('{}', $$ "
        "MATCH(A {{id:1234}})<-[R]-(B) "
        "return A,R,B "
        "$$) as (Node1 agtype, relation agtype, Node2 agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery("fetch_specific_segment", std::format(
        "select * from cypher('{}',$$ "
        "match(a:segment {{id: 23781}})-[b]-(c) "
        "return count(b) "
        "$$) as (a agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery("fetch_specific_shortcut", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (a:segment{{id:1}}),(b:segment{{id:10}})  "
        "CREATE (a)-[s:Shortcut]->(b)  "
        "$$) AS (s agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery("set_shortcut_property", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:Shortcut]->() "
        "SET s.Lightning_route = \"This is quick\" "
        "$$) as (s agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery("nullify_shortcut_property", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:Shortcut]->() "
        "SET s.Lightning_route = NULL "
        "$$) as (s agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery("fetch_trajectories", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (t:Trajectory)  "
        "DETACH DELETE t "
        "$$) as (t agtype); "
        , db.graph_name_)

    );
    
    db.BenchmarkQuery("delete_shortcuts", std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH ()-[s:Shortcut]->() "
        "DELETE s "
        "$$) as (s agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery("fetch_neighbourhood", std::format(
        "SELECT * from cypher('{}', $$ "
        "MATCH p = (A:segment {{id:617393}})-[R*1..N]->(B:segment {{id:617394}}) "
        "return p "
        "limit 1 "
        "$$) as (path agtype); "
        , db.graph_name_)

    );

    db.BenchmarkQuery("fetch_bridge",  std::format(
        "SELECT * FROM cypher('{}', $$ "
        "MATCH (s:segment)-[c:connected_to]-() "
        "WITH r, count(c) as NeighbourCount "
        "WHERE NeighbourCount=1 "
        "RETURN r "
        "$$) as (r agtype); "
        , db.graph_name_)

    );

    return 0; 
}
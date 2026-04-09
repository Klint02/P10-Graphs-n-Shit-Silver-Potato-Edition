#include <iostream>
#include <libpq-fe.h>
#include "DBfunctions.hpp"

//query used in age viewer to show nodes and edges. Should use limit for speedup.
// select * from cypher('dummy_graph',$$ match(n)-[r]->(m) return n,r,m limit 100 $$) as (n agtype, r agtype, m agtype);
// select * from cypher('dummy_graph',$$ match(n) detach delete n $$) as (n agtype); -> deletes all data in the graph.
int main() {
    //Making connection.
    const char* conninfo = "host=127.0.0.1 port=5430 dbname=postgresDB user=postgresUser password=postgresPW";
    PGconn* conn = PQconnectdb(conninfo);

    // checks for connection to DB if not, gives and error.
    if(PQstatus(conn) != CONNECTION_OK){
        std::cerr << "Connection failed, maybe check if Docker container is running" << std::endl;
        return 1;
    }

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
    return 0;
}
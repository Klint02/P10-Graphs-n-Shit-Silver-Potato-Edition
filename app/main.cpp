#include <iostream>
#include <libpq-fe.h>
#include "DBfunctions.hpp"

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

    //query used to test if it is possible to return data from apache age.
    const char* query = "select * from cypher('demo_graph',$$ match(n:Person)-[r]->(m:Person) return n.name,type(r),m.name $$) as (n agtype, r agtype, m agtype);";

    //returnResult is from the library in src/DBfunctions.cpp
    std::vector<std::vector<std::string>> test = returnResult(conn,query);

    //for print/debug
    for(auto item : test){
        for(auto val:item){
            std::cout << val;
        }
        std::cout << std::endl;
    }

    //PQclear(res);

    //Closes connection to DB.
    PQfinish(conn);
    return 0;
}
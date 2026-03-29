#include "DBfunctions.hpp"

std::vector<std::vector<std::string>> returnResult(PGconn* conn, const char* query){

    std::vector<std::vector<std::string>> result;

    PGresult* res= PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Query failed: " << PQerrorMessage(conn) << std::endl;
        PQclear(res);
        PQfinish(conn);
        return result;
    }

    int rows = PQntuples(res);
    int cols = PQnfields(res);

    for (int i = 0; i < rows; i++){
        std::vector<std::string> arr;
        for (int j = 0; j < cols; j++){
            arr.push_back(PQgetvalue(res,i,j));
        }
        result.push_back(arr);
    }   

    PQclear(res);
    return result;
} 



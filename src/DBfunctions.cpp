#include "DBfunctions.hpp"

//function that returns data based on the query it gets.
std::vector<std::vector<std::string>> returnResult(PGconn* conn, const char* query){
    
    //Result variable
    std::vector<std::vector<std::string>> result;

    //Checks if the query went through otherwise gives an error.
    PGresult* res= PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Query failed: " << PQerrorMessage(conn) << std::endl;
        PQclear(res);
        PQfinish(conn);
        return result;
    }

    //amount of rows and columns in the data returns
    int rows = PQntuples(res);
    int cols = PQnfields(res);

    //add the data to a  matrix[row x col].  
    for (int i = 0; i < rows; i++){
        std::vector<std::string> arr;
        for (int j = 0; j < cols; j++){
            arr.push_back(PQgetvalue(res,i,j));
        }
        result.push_back(arr);
    }   

    //clears the result for new results and returns the matrix. 
    PQclear(res);
    return result;
} 

//Extracts the data from csv file and returns the result as a matrix[rows X cols] of type strings.
std::vector<std::vector<std::string>> extract_Data_From_CSV(const std::string& filepath){

    //var declaration.
    std::vector<std::vector<std::string>> result;
    std::ifstream file(filepath);

    //checks of file is open, otherwose throws error.
    if(!file.is_open()){
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return result; 
    }

    std::string line;

    //skips first line
    std::getline(file,line);

    //goes though the first 10 lines for simplicity 
    //TODO(RBN): Make it go through all lines.
    for(int i = 0; i < 10; i++)
    {
        std::getline(file,line);
        std::vector<std::string> row;
        std::stringstream ss(line);
        std::string cell;
        for (int j = 0; j < 3; j++)
        {
            std::getline(ss,cell,',');
            row.push_back(cell);
        }
        result.push_back(row);
    }

    //closes file and returns result as a matrix of strings
    file.close();
    return result;
}

//Pretty for terminal 
void debug_print(std::vector<std::vector<std::string>>& table){
    
     std::vector<size_t> colWidths;

    for (const auto& row : table) {
        if (colWidths.size() < row.size())
            colWidths.resize(row.size(), 0);

        for (size_t i = 0; i < row.size(); ++i) {
            colWidths[i] = std::max(colWidths[i], row[i].length());
        }
    }

    // Print table
    for (const auto& row : table) {
        for (size_t i = 0; i < row.size(); ++i) {
            std::cout << std::left << std::setw(colWidths[i] + 2) << row[i];
        }
        std::cout << std::endl;
    }
}

//takes data extracted from csv and insert into apache age.
bool insert_segments_as_nodes(PGconn* conn, const std::vector<std::vector<std::string>>& data){
    
    //var declarations
    std::string start = "select * from cypher('dummy_graph',$$ unwind [";

    std::string end = "] as row MERGE (s:Segment {id: row.id}) "
                      "SET s.start_point = row.start_point, "
                      "s.end_point = row.end_point $$) as (n agtype);";
    std::string middle;

    //takes the extracted CSV data and makes a node for every row in the matrix.
    for(const auto& row:data){
        middle+= "{id:" + row[0] + ", start_point: " + row[1] + ", end_point: " +row[2]+"},";
    }

    //pops the last ',' the loop above makes. 
    if (!middle.empty()) middle.pop_back();

    //concatenates the 3 strings and cast it to c_str() for the query.
    std::string queryFusion = start + middle + end;
    const char* query = queryFusion.c_str();

    //executes query and checks is it goes though. if not, throws error and retuen false.
    PGresult* res= PQexec(conn, query);    
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "insert_segements_as_nodes() failed: " << PQerrorMessage(conn) << std::endl;
        PQclear(res);
        PQfinish(conn);
        return false;
    }
    
   
    return true;
}

//adds edges between nodes where either start_point or end_point overlaps. Does not take direction into consideration.
//TODO(RBN): make Check for directions.
bool add_edges(PGconn* conn){

    //var declarations
    const char* query = "select * from cypher('dummy_graph',$$ match (n:Segment),(m:Segment) where n.id <> m.id  AND  (n.start_point = m.start_point or n.start_point = m.end_point or n.end_point = m.start_point or n.end_point = m.end_point) MERGE (n)-[e:CONNECTED_TO]->(m) RETURN e $$) as (e agtype);";
    
    //Executes query and stores result.
    PGresult* res = PQexec(conn,query);
    if(PQresultStatus(res) != PGRES_TUPLES_OK){
        std::cerr << "add_edges() Failed to execute" <<PQerrorMessage(conn)<< std::endl;
        PQclear(res);
        return false; 
    } 


    return true;
}



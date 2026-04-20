#include "DBfunctions.hpp"
#include <unordered_map>
#include <set>
#include <sstream>
#include <unordered_set>

//function that returns data based on the query it gets.
db_result_t returnResult(PGconn* conn, const char* query){
    
    //Result variable
    db_result_t result;

    //Checks if the query went through otherwise gives an error.
    PGresult* res= PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Query failed: " << PQerrorMessage(conn) << std::endl;
        PQclear(res);
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
db_result_t extract_Data_From_CSV(const std::string& filepath){
    auto start_timer{std::chrono::steady_clock::now()};

    //var declaration.
    db_result_t result;
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
    //for(int i = 0; i < 10; i++)
    while(std::getline(file,line)) //FOR WHILE-LOOP
    {
        //std::getline(file,line); USE FOR FOR-LOOP
        std::vector<std::string> row;
        std::stringstream ss(line);
        std::string cell;

        //loops through the cells in the current row.
        int j=0;
        while(std::getline(ss,cell,','))
        {   
            //checks if the current cell is data we want to extract.
            //segmentkey=0, startpoint=1, endpoint=2, category=5, direction=6, streetname= 14;
            if(j==0 ||j==1 ||j==2 ||j==5 ||j==6 ||j==14){
                row.push_back(cell);
            }
            j++;
        }
        ss.str(""); ss.clear(); 
        result.push_back(row);
    }

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish-start_timer};

    std::cout<< "Extract_Data_From_CSV() took: " << time_elapsed.count() <<" seconds" << std::endl;
    
    //closes file and returns result as a matrix of strings
    file.close();
    result.shrink_to_fit();
    return result;
}

//Pretty for terminal 
void debug_print(db_result_t& table){
    
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
bool insert_segments_as_nodes(PGconn* conn, const db_result_t& data){
    auto start_timer{std::chrono::steady_clock::now()};
    const size_t BATCH_SIZE = 1000;
    //var declarations
    std::string start = "select * from cypher('dummy_graph',$$ unwind [";

    std::string end = "] as row MERGE (s:Segment {id: row.id}) "
                      "SET s.start_point = row.start_point, "
                      "s.end_point = row.end_point, s.category = row.category, s.direction = row.direction, s.name = row.name $$) as (n agtype);";

    //takes the extracted CSV data and makes a node for every row in the matrix.

    for(size_t i = 0; i<data.size(); i+=BATCH_SIZE){
        std::string middle="";
        for (size_t j = i; j < i+BATCH_SIZE && j<data.size(); ++j)
        {
            middle += "{id:" + data[j][0] + ", start_point: " + data[j][1] + ", end_point: " + data[j][2] + ", category: " + data[j][3] + ", direction: " + data[j][4] + ", name: " + data[j][5]+  "},";      
        }
        if (!middle.empty()) middle.pop_back();
        std::string query = start + middle + end;
        PGresult* res= PQexec(conn, query.c_str());    
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::cerr << "insert_segements_as_nodes() failed: " << PQerrorMessage(conn) << std::endl;
            PQclear(res);
            return false;
        }
        PQclear(res);

    }

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish-start_timer};

    std::cout<< "insert_segments_as_nodes() took: " << time_elapsed.count() <<" seconds" << std::endl;

    
    
    return true;
}

//adds edges between nodes where either start_point or end_point overlaps. Does not take direction into consideration.
//TODO(RBN): make Check for directions.
bool add_edges(PGconn* conn, db_result_t& data){

    auto start_timer{std::chrono::steady_clock::now()};

    std::string start = "SELECT * FROM cypher('dummy_graph',$$UNWIND [ ";
    std::string end =   "] AS pair "
                        "MATCH (a:Segment {id: pair[0]}), (b:Segment {id: pair[1]}) "
                        "MERGE (a)-[:CONNECTED_TO]->(b) "
                        "$$) AS (e agtype);";
    
        std::set<std::pair<std::string,std::string>> edges;
        std::unordered_map<std::string,std::vector<std::string>> point_map;

        // Build map: point -> segment IDs
        for (const auto& segment : data) {
            point_map[segment[1]].push_back(segment[0]);
            point_map[segment[2]].push_back(segment[0]);
        }

        // Build edges
        for (const auto& segment : data) {
            const std::string& seg_id   = segment[0];
            const std::string& seg_start = segment[1];
            const std::string& seg_end   = segment[2];

            auto add_edges = [&](const std::string& point) {
                for (const auto& other_id : point_map[point]) {
                    if (seg_id != other_id) {
                        edges.insert({seg_id, other_id});
                    }
                }
            };

            add_edges(seg_start);
            add_edges(seg_end);
        }

        // Build query string
        std::ostringstream middle;
        for (const auto& [a, b] : edges) {
            middle << "[" << a << "," << b << "],";
        }
        std::string middle_str = middle.str();
        if (!middle_str.empty()) middle_str.pop_back();

    //var declarations
    std::string fill_query = start + middle_str + end; 
             

    PQexec(conn, "BEGIN");

    //Executes query and stores result.
    PGresult* res = PQexec(conn,fill_query.c_str());
    if(PQresultStatus(res) != PGRES_TUPLES_OK){
        std::cerr << "add_edges() Failed to execute" <<PQerrorMessage(conn)<< std::endl;
        PQclear(res);

        PQexec(conn, "ROLLBACK");
        return false; 
    }

    PQclear(res);
    PQexec(conn, "COMMIT");

    auto finish2{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed2{finish2-start_timer};
    std::cout<< "add_edges() took: " << time_elapsed2.count() <<" seconds" << std::endl;


    return true;
}



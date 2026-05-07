#include "DBfunctions.hpp"
#include <unordered_map>
#include <map>
#include <set>
#include <sstream>
#include <format>

#include <unordered_set>

DBfunctions::DBfunctions(const std::string& host,
                const std::string& port,
                const std::string& dbname,
                const std::string& username,
                const std::string& password,
                const std::string& graph_prefix,
                const std::string& graph_name)
                : graph_name_(graph_name)
{
    //Making connection.
    const std::string conninfo = "host=" + host + " port=" + port + " dbname=" + dbname + " user=" + username + " password=" + password;
    conn_ = PQconnectdb(conninfo.c_str());
    // checks for connection to DB if not, gives and error.
    switch (PQstatus(conn_))
    {
    case CONNECTION_OK:
        std::cout << "Connection to postgres succesful" << std::endl; 
        //Declares search path for apache age
        res_ = PQexec(conn_, R"(set search_path = ag_catalog, "$user", public;)"); 
        PQclear(res_);  
        res_ = PQexec(conn_, std::format("SELECT * FROM ag_catalog.drop_graph('{}', true)", graph_name).c_str()); 
        PQclear(res_);  
        res_ = PQexec(conn_, std::format("SELECT * FROM ag_catalog.create_graph('{}');", graph_name).c_str());   
        PQclear(res_);  
        break;
    default:
        std::cerr << "Connection failed, maybe check if Docker container is running" << std::endl;
        break;
    }


}

bool DBfunctions::ResetGraph()
{
    //TODO(NKC): implement error checking
    res_ = PQexec(conn_, std::format("SELECT * FROM ag_catalog.drop_graph('{}', true)", graph_name_).c_str()); 
    res_ = PQexec(conn_, std::format("SELECT * FROM ag_catalog.create_graph('{}');", graph_name_).c_str());   
    return true;
}

bool DBfunctions::CreateMunicipalities() 
{
    
    auto start_timer{std::chrono::steady_clock::now()};

    //TODO(NKC): implement error checking
    db_result_t municipalities = returnResult(conn_, "SELECT * FROM regions.dk_municipalities ORDER BY dk_municipalitykey");
    
    std::string current_city;
    std::string current_region;
    uint8_t sub_municipality_count = 1;
    std::cout << municipalities.size() << std::endl;
    
    for (const auto& sub_municipality : municipalities) {
        if (current_region.compare(sub_municipality.at(2))) {
            //std::cout << current_region << " is not " << sub_municipality.at(2) << std::endl;
            current_region = sub_municipality.at(2);
            PGresult* res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ MERGE (:region {{name: '{}', region_code: '{}'}}) $$) as (n agtype)", graph_name_, sub_municipality.at(4), sub_municipality.at(2)).c_str());
            PQclear(res);
        }
        if (current_city.compare(sub_municipality.at(1))) {
            sub_municipality_count = 1;
            //std::cout << current_city << " is not " << sub_municipality.at(1) << std::endl;
            current_city = sub_municipality.at(1);
            PGresult* res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ MERGE (:municipality {{name: '{}', code: '{}'}}) $$) as (n agtype)", graph_name_, sub_municipality.at(3), sub_municipality.at(1)).c_str());
            PGresult* res1 = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ MATCH (a:region), (b:municipality) WHERE a.region_code = '{}' AND b.code = '{}' CREATE (a)-[e:contains]->(b) RETURN e $$) as (e agtype)", graph_name_, sub_municipality.at(2), sub_municipality.at(1)).c_str());
            PQclear(res);
            PQclear(res1);
            
        }
        PGresult* res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ CREATE (:sub_municipality {{name: '{}-{}', dk_municipalitykey: '{}'}}) $$) as (n agtype)", graph_name_, sub_municipality_count, sub_municipality.at(3), sub_municipality.at(0)).c_str());
        PGresult* res1 = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ MATCH (a:municipality), (b:sub_municipality) WHERE a.code = '{}' AND b.dk_municipalitykey = '{}' CREATE (a)-[e:contains]->(b) RETURN e $$) as (e agtype)", graph_name_, sub_municipality.at(1), sub_municipality.at(0)).c_str());
        PQclear(res);
        PQclear(res1);
        
        sub_municipality_count += 1;
    }
    
    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish-start_timer};
    std::cout<< "CreateMunicipalities() took: " << time_elapsed.count() <<" seconds" << std::endl;

    return true;
}

bool DBfunctions::CreateNodesForAllSubMunicipalities() {
    
    auto start_timer{std::chrono::steady_clock::now()};

    db_result_t municipalities = returnResult(conn_, "SELECT dk_municipalitykey FROM regions.dk_municipalities ORDER BY dk_municipalitykey");
    std::map<std::string,Segment> segment_map; 
    for (const auto& sub_municipality : municipalities) {
        std::string sub_municipality_string = sub_municipality.at(0);
        std::cout << sub_municipality_string << std::endl;
        db_result_t segments = returnResult(conn_, std::format("SELECT segmentkey, startpoint, endpoint, category, direction, name FROM maps.osm_dk_20140101 WHERE ST_Intersects(segmentgeo::geometry, (SELECT ST_Union(geog::geometry) FROM regions.dk_municipalities WHERE dk_municipalitykey in ({})));" , sub_municipality.at(0)).c_str());       
        for (const auto& segment : segments) {
            
            if (segment_map.contains(segment.at(0))) {
                segment_map[segment.at(0)].municipality_keys.emplace_back(sub_municipality_string);
            } else {
                segment_map[segment.at(0)] = {};
                segment_map[segment.at(0)].segmentkey = segment.at(0);
                segment_map[segment.at(0)].startpoint = segment.at(1);
                segment_map[segment.at(0)].endpoint = segment.at(2);
                segment_map[segment.at(0)].category = segment.at(3);
                segment_map[segment.at(0)].direction = segment.at(4);
                segment_map[segment.at(0)].name = segment.at(5);
                segment_map[segment.at(0)].municipality_keys.emplace_back(sub_municipality_string);
            }
        }
    }
    int it = 1;

    //TODO(nkc): batch up to 2000 segments per database request
    for (const auto& [key, data] : segment_map) {
        if (it % 50 == 0) {
            std::cout << it << std::endl;
        }

        switch (data.municipality_keys.size())
        {
        case 1:
            PQclear(PQexec(conn_, std::format(
                "SELECT * FROM cypher('{}', $$ "
                "MATCH (a:sub_municipality) "
                "WHERE a.dk_municipalitykey = '{}' "
                "CREATE (a)-[:contains]->(:segment {{segmentkey: '{}', startpoint: '{}', endpoint: '{}', category: '{}', direction: '{}', name: '{}' }}) "
                "$$) as (n agtype);", 
                graph_name_, 
                data.municipality_keys.at(0), 
                data.segmentkey, 
                data.startpoint, 
                data.endpoint, 
                data.category, 
                data.direction, 
                data.name
            ).c_str()));
            break;
        case 2:
            PQclear(PQexec(conn_, std::format(
                "SELECT * FROM cypher('{}', $$ "
                "MATCH (a:sub_municipality), (b:sub_municipality) "
                "WHERE a.dk_municipalitykey = '{}' AND b.dk_municipalitykey = '{}' "
                "CREATE (a)-[:contains]->(:segment {{segmentkey: '{}', startpoint: '{}', endpoint: '{}', category: '{}', direction: '{}', name: '{}' }})"
                "<-[:contains]-(b) "
                "$$) as (n agtype);", 
                graph_name_, 
                data.municipality_keys.at(0), 
                data.municipality_keys.at(1), 
                data.segmentkey, 
                data.startpoint, 
                data.endpoint, 
                data.category, 
                data.direction, 
                data.name
            ).c_str()));
            break;
        case 3:
            PQclear(PQexec(conn_, std::format(
                "SELECT * FROM cypher('{}', $$ "
                "MATCH (a:sub_municipality), (b:sub_municipality), (c:sub_municipality) "
                "WHERE a.dk_municipalitykey = '{}' AND b.dk_municipalitykey = '{}' AND c.dk_municipalitykey = '{}' "
                "CREATE (s:segment {{segmentkey: '{}', startpoint: '{}', endpoint: '{}', category: '{}', direction: '{}', name: '{}' }}), "
                "(a)-[:contains]->(s), (b)-[:contains]->(s), (c)-[:contains]->(s) "
                "$$) as (n agtype);",
                graph_name_,
                data.municipality_keys.at(0),
                data.municipality_keys.at(1),
                data.municipality_keys.at(2),
                data.segmentkey,
                data.startpoint,
                data.endpoint,
                data.category,
                data.direction,
                data.name
            ).c_str()));
            break;
        default:
            std::cerr << "Unhandled size " << data.municipality_keys.size() << std::endl;
            std::cout << "Key: " << key << ", subs: ";
            
            for (const auto& el : data.municipality_keys) {
                std::cout << el << " "; 
            }
            std::cout << std::endl;
            break;
        }
        it += 1;
        /*
        if (data.size() > 2) {
            
        std::cout << "Key: " << key << ", subs: ";
        
        for (const auto& el : data) {
            std::cout << el << " "; 
        }
        std::cout << std::endl;
        }
        */
    }
    
    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish-start_timer};
    std::cout<< "CreateNodesForAllSubMunicipalities() took: " << time_elapsed.count() <<" seconds" << std::endl;

    return true;
}

bool DBfunctions::CreateEdgesForAllSegments(){
    
    auto start_timer{std::chrono::steady_clock::now()};

    PQclear(PQexec(conn_, "BEGIN"));

    for(int i=303;i<311;i++){
        std::cout << i << std::endl;
        
        auto start_timer2{std::chrono::steady_clock::now()};

        PGresult* resBB = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
        "MATCH (su:sub_municipality {{dk_municipalitykey: '{}'}})-[:contains]->(a:segment {{direction: 'BOTH'}}), (su:sub_municipality {{dk_municipalitykey: '{}'}})-[:contains]->(b:segment {{direction: 'BOTH'}}) "
        "WHERE a.segmentkey <> b.segmentkey and (a.startpoint = b.startpoint or a.startpoint = b.endpoint or a.endpoint = b.startpoint or a.endpoint = b.endpoint) "
        "CREATE (a)-[c:connected_to]->(b) "
        "RETURN properties(a), c, properties(b) $$) as (a agtype, c agtype, b agtype);", graph_name_, i, i).c_str());
        
        if(PQresultStatus(resBB) != PGRES_TUPLES_OK){
            std::cerr << "Query for BOTH->BOTH Failed to execute " <<PQerrorMessage(conn_)<< std::endl;
            PQclear(resBB);
                    
            PQclear(PQexec(conn_, "ROLLBACK"));
            return false; 
        }
        
        auto query1{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed1{query1-start_timer2};
        std::cout<< "BOTH->BOTH took: " << time_elapsed1.count() <<" seconds" << std::endl;

        PGresult* resBF = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
        "MATCH (su:sub_municipality {{dk_municipalitykey: '{}'}})-[:contains]->(a:segment {{direction: 'BOTH'}}), (su:sub_municipality {{dk_municipalitykey: '{}'}})-[:contains]->(b:segment {{direction: 'FORWARD'}}) "
        "WHERE a.startpoint = b.startpoint or a.endpoint = b.startpoint "
        "MERGE (a)-[c:connected_to]->(b) "
        "RETURN properties(a), c, properties(b) $$) as (a agtype, c agtype, b agtype);", graph_name_, i, i).c_str());

        if(PQresultStatus(resBF) != PGRES_TUPLES_OK){
            std::cerr << "Query for BOTH->FORWARD Failed to execute " <<PQerrorMessage(conn_)<< std::endl;
            PQclear(resBF);
            
            PQclear(PQexec(conn_, "ROLLBACK"));
            return false; 
        }
        
        auto query2{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed2{query2-query1};
        std::cout<< "BOTH->FORWARD took: " << time_elapsed2.count() <<" seconds" << std::endl;

        PGresult* resFB = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
        "MATCH (su:sub_municipality {{dk_municipalitykey: '{}'}})-[:contains]->(a:segment {{direction: 'FORWARD'}}), (su:sub_municipality {{dk_municipalitykey: '{}'}})-[:contains]->(b:segment {{direction: 'BOTH'}}) "
        "WHERE a.endpoint = b.startpoint or a.endpoint = b.endpoint "
        "MERGE (a)-[c:connected_to]->(b) "
        "RETURN properties(a), c, properties(b) $$) as (a agtype, c agtype, b agtype);", graph_name_, i, i).c_str());
        
        if(PQresultStatus(resFB) != PGRES_TUPLES_OK){
            std::cerr << "Query for FORWARD->BOTH Failed to execute " <<PQerrorMessage(conn_)<< std::endl;
            PQclear(resFB);
            
            PQclear(PQexec(conn_, "ROLLBACK"));
            return false; 
        }
        
        auto query3{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed3{query3-query2};
        std::cout<< "FORWARD->BOTH took: " << time_elapsed3.count() <<" seconds" << std::endl;

        PGresult* resFF = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
        "MATCH (su:sub_municipality {{dk_municipalitykey: '{}'}})-[:contains]->(a:segment {{direction: 'FORWARD'}}), (su:sub_municipality {{dk_municipalitykey: '{}'}})-[:contains]->(b:segment {{direction: 'FORWARD'}}) "
        "WHERE a.endpoint = b.startpoint "
        "CREATE (a)-[c:connected_to]->(b) "
        "RETURN properties(a), c, properties(b) $$) as (a agtype, c agtype, b agtype);", graph_name_, i, i).c_str());

        if(PQresultStatus(resFF) != PGRES_TUPLES_OK){
            std::cerr << "Query for FORWARD->FORWARD Failed to execute " <<PQerrorMessage(conn_)<< std::endl;
            PQclear(resFF);
            
            PQclear(PQexec(conn_, "ROLLBACK"));
            return false; 
        }
        
        auto query4{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed4{query4-query3};
        std::cout<< "FORWARD->FORWARD took: " << time_elapsed4.count() <<" seconds" << std::endl;
        
        PQclear(resBB);
        PQclear(resBF);
        PQclear(resFB);
        PQclear(resFF);
    }

    PQclear(PQexec(conn_, "COMMIT"));

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish-start_timer};
    std::cout<< "CreateEdgesForAllSegments() took: " << time_elapsed.count() <<" seconds" << std::endl;

    return true;
}

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



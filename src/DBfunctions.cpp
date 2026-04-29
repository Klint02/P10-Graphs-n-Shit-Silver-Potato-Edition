#include "DBfunctions.hpp"
#include <unordered_map>
#include <set>
#include <sstream>
#include <format>

#include <unordered_set>

DBfunctions::DBfunctions(const std::string &host,
                         const std::string &port,
                         const std::string &dbname,
                         const std::string &username,
                         const std::string &password,
                         const std::string &graph_prefix,
                         const std::string &graph_name)
    : graph_name_(graph_name)
{
    // Making connection.
    const std::string conninfo = "host=" + host + " port=" + port + " dbname=" + dbname + " user=" + username + " password=" + password;
    conn_ = PQconnectdb(conninfo.c_str());
    // checks for connection to DB if not, gives and error.
    switch (PQstatus(conn_))
    {
    case CONNECTION_OK:
        std::cout << "Connection to postgres succesful" << std::endl;
        // Declares search path for apache age
        res_ = PQexec(conn_, R"(set search_path = ag_catalog, "$user", public;)");
        PQclear(res_);
        std::cout << "Dropping old graph" << std::endl;
        res_ = PQexec(conn_, std::format("SELECT * FROM ag_catalog.drop_graph('{}', true)", graph_name).c_str());
        if (PQresultStatus(res_) != PGRES_TUPLES_OK) std::cout <<"WTF MAN DEN SLETTER IKKE!" <<std::endl;
        std::cout << "Creating new grap" << std::endl;
        res_ = PQexec(conn_, std::format("SELECT * FROM ag_catalog.create_graph('{}');", graph_name).c_str());
        PQclear(res_);
        std::cout << "Creating index for node id" << std::endl;
        res_ = PQexec(conn_, std::format("CREATE INDEX ON {}.\"Segment\" (id);", graph_name).c_str());
        PQclear(res_);

        break;
    default:
        std::cerr << "Connection failed, maybe check if Docker container is running" << std::endl;
        break;
    }
}

bool DBfunctions::ResetGraph()
{
    // TODO(NKC): implement error checking
    res_ = PQexec(conn_, std::format("SELECT * FROM ag_catalog.drop_graph('{}', true)", graph_name_).c_str());
    res_ = PQexec(conn_, std::format("SELECT * FROM ag_catalog.create_graph('{}');", graph_name_).c_str());
    return true;
}

bool DBfunctions::CreateMunicipalities()
{
    // TODO(NKC): implement error checking
    db_result_t municipalities = returnResult(conn_, "SELECT * FROM regions.dk_municipalities ORDER BY dk_municipalitykey");

    std::string current_city;
    std::string current_region;
    uint8_t sub_municipality_count = 1;
    std::cout << municipalities.size() << std::endl;
    std::vector<std::string> municipality_codes;
    for (const auto &sub_municipality : municipalities)
    {

        if (current_region.compare(sub_municipality.at(2)))
        {
            if (current_region.size() != 0)
            {
                std::string codes = "";
                for (int i = 0; i < municipality_codes.size(); i++)
                {
                    codes += municipality_codes.at(i);
                    if (!(i == municipality_codes.size() - 1))
                        codes += ",";
                }

                add_edges_between_segments(returnResult(conn_, std::format("SELECT * FROM maps.osm_dk_20140101 WHERE ST_Within(segmentgeo::geometry, (SELECT ST_Union(geog::geometry) FROM regions.dk_municipalities WHERE dk_municipalitykey in ({})));", codes).c_str()));

                municipality_codes.clear();
            }
            std::cout << current_region << " is not " << sub_municipality.at(2) << std::endl;
            current_region = sub_municipality.at(2);
            PGresult *res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ CREATE (:region {{name: '{}', region_code: '{}'}}) $$) as (n agtype)", graph_name_, sub_municipality.at(4), sub_municipality.at(2)).c_str());
        }
        if (current_city.compare(sub_municipality.at(1)))
        {
            sub_municipality_count = 1;
            std::cout << current_city << " is not " << sub_municipality.at(1) << std::endl;
            current_city = sub_municipality.at(1);
            PGresult *res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ CREATE (:municipality {{name: '{}', code: '{}'}}) $$) as (n agtype)", graph_name_, sub_municipality.at(3), sub_municipality.at(1)).c_str());
            PGresult *res1 = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ MATCH (a:region), (b:municipality) WHERE a.region_code = '{}' AND b.code = '{}' CREATE (a)-[e:contains]->(b) RETURN e $$) as (e agtype)", graph_name_, sub_municipality.at(2), sub_municipality.at(1)).c_str());
        }
        municipality_codes.emplace_back(sub_municipality.at(0));
        std::cout << sub_municipality.at(0) << " " << sub_municipality.at(3) << "-" << sub_municipality_count << std::endl;
        PGresult *res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ CREATE (:sub_municipality {{name: '{}-{}', dk_municipalitykey: '{}'}}) $$) as (n agtype)", graph_name_, sub_municipality_count, sub_municipality.at(3), sub_municipality.at(0)).c_str());
        PGresult *res1 = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ MATCH (a:municipality), (b:sub_municipality) WHERE a.code = '{}' AND b.dk_municipalitykey = '{}' CREATE (a)-[e:contains]->(b) RETURN e $$) as (e agtype)", graph_name_, sub_municipality.at(1), sub_municipality.at(0)).c_str());
        
        insert_segments_as_nodes(returnResult(conn_, std::format("SELECT * FROM maps.osm_dk_20140101 WHERE ST_Within(segmentgeo::geometry, (SELECT ST_Union(geog::geometry) FROM regions.dk_municipalities WHERE dk_municipalitykey in ('{}')));", sub_municipality.at(0)).c_str()));
        add_edges_between_segments_and_submunicipality(returnResult(conn_, std::format("SELECT segmentkey FROM maps.osm_dk_20140101 WHERE ST_Within(segmentgeo::geometry, (SELECT ST_Union(geog::geometry) FROM regions.dk_municipalities WHERE dk_municipalitykey in ('{}')));", sub_municipality.at(0)).c_str()),sub_municipality.at(0));
        sub_municipality_count += 1;
    }
    

    return true;
}

// takes data extracted from csv and insert into apache age.
bool DBfunctions::insert_segments_as_nodes(const db_result_t &data)
{
    auto start_timer{std::chrono::steady_clock::now()};
    const size_t BATCH_SIZE = 3000;

    // var declarations
    std::string start = std::format("SELECT * FROM cypher('{}',$$UNWIND [ ", graph_name_);

    std::string end = "] as row CREATE (:Segment {id: row.id, start_point: row.start_point, end_point: row.end_point, category: row.category, direction: row.direction, name: row.name}) $$) as (a agtype);";
                      
    // takes the extracted CSV data and makes a node for every row in the matrix.
    int countingDown = data.size();
    auto batch_timer{std::chrono::steady_clock::now()};


    for (size_t i = 0; i < data.size(); i += BATCH_SIZE)
    {
        batch_timer = std::chrono::steady_clock::now();
        size_t batch_end = std::min(i + BATCH_SIZE, data.size());
        std::string middle = "";
        for (size_t j = i; j < batch_end; j++)
        {
            if (data[j][14].contains('"'))
            {
                std::string temp = data[j][14];
                temp.erase(std::remove(temp.begin(), temp.end(), '"'), temp.end());
                middle += std::format("{{id: {}, start_point: {}, end_point: {}, category: \"{}\", direction: \"{}\", name: \"{}\"}},", data[j][0], data[j][1], data[j][2], data[j][5], data[j][6], temp);
            }
            else
            {
                middle += std::format("{{id: {}, start_point: {}, end_point: {}, category: \"{}\", direction: \"{}\", name: \"{}\"}},", data[j][0], data[j][1], data[j][2], data[j][5], data[j][6], data[j][14]);
            }
        }

        if (!middle.empty())
            middle.pop_back();
        std::string query = start + middle + end;
        PGresult *res = PQexec(conn_, query.c_str());
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            std::cerr << "insert_segements_as_nodes() failed: " << PQresultErrorMessage(res) << std::endl;
            // std::cout << middle <<std::endl;
            PQclear(res);
            return false;
        }
        PQclear(res);

        auto batch_finish{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed{batch_finish - batch_timer};
        std::cout << "Batch took: " << time_elapsed.count() << " seconds" << std::endl;

        countingDown -= (batch_end - i);
        std::cout << "Elements left: " << countingDown << std::endl;
    }

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish - start_timer};

    std::cout << "insert_segments_as_nodes() Insert: " << time_elapsed.count() << " seconds" << std::endl;

    return true;
}

// function that returns data based on the query it gets.
db_result_t returnResult(PGconn *conn, const char *query)
{

    // Result variable
    db_result_t result;

    // Checks if the query went through otherwise gives an error.
    PGresult *res = PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::cerr << "Query failed: " << PQerrorMessage(conn) << std::endl;
        PQclear(res);
        return result;
    }

    // amount of rows and columns in the data returns
    int rows = PQntuples(res);
    int cols = PQnfields(res);

    // add the data to a  matrix[row x col].
    for (int i = 0; i < rows; i++)
    {
        std::vector<std::string> arr;
        for (int j = 0; j < cols; j++)
        {
            arr.push_back(PQgetvalue(res, i, j));
        }
        result.push_back(arr);
    }

    // clears the result for new results and returns the matrix.
    PQclear(res);
    return result;
}
/*
//Extracts the data from csv file and returns the result as a matrix[rows X cols] of type strings.
db_result_t segments(){
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
*/
// Pretty for terminal
void debug_print(db_result_t &table)
{

    std::vector<size_t> colWidths;

    for (const auto &row : table)
    {
        if (colWidths.size() < row.size())
            colWidths.resize(row.size(), 0);

        for (size_t i = 0; i < row.size(); ++i)
        {
            colWidths[i] = std::max(colWidths[i], row[i].length());
        }
    }

    // Print table
    for (const auto &row : table)
    {
        for (size_t i = 0; i < row.size(); ++i)
        {
            std::cout << std::left << std::setw(colWidths[i] + 2) << row[i];
        }
        std::cout << std::endl;
    }
}

// adds edges between nodes where either start_point or end_point overlaps. Does not take direction into consideration.
// TODO(RBN): make Check for directions.
bool DBfunctions::add_edges_between_segments(const db_result_t &data)
{

    auto start_timer{std::chrono::steady_clock::now()};

    std::string start = std::format("SELECT * FROM cypher('{}',$$UNWIND [ ", graph_name_);
    std::string end = "] AS pair "
                      "MATCH(a:Segment {id:pair[0]}), (b:Segment {id:pair[1]})"
                      "CREATE (a)-[:CONNECTED_TO]->(b) "
                      "$$) AS (e agtype);";

    std::set<std::pair<std::string, std::string>> edges;
    std::unordered_map<std::string, std::vector<std::string>> point_map;

    // Build map: point -> segment IDs
    for (const auto &segment : data)
    {
        point_map[segment[1]].push_back(segment[0]);
        point_map[segment[2]].push_back(segment[0]);
    }

    // Build edges
    for (const auto &segment : data)
    {
        const std::string &seg_id = segment[0];
        const std::string &seg_start = segment[1];
        const std::string &seg_end = segment[2];

        auto add_edges = [&](const std::string &point)
        {
            for (const auto &other_id : point_map[point])
            {
                if (seg_id != other_id)
                {
                    edges.insert({seg_id, other_id});
                }
            }
        };

        add_edges(seg_start);
        add_edges(seg_end);
    }

    // Build query string
    std::ostringstream middle;
    for (const auto &[a, b] : edges)
    {
        middle << "[" << a << "," << b << "],";
    }
    std::string middle_str = middle.str();
    if (!middle_str.empty())
        middle_str.pop_back();

    // var declarations
    std::string fill_query = start + middle_str + end;

    // Executes query and stores result.
    PGresult *res = PQexec(conn_, fill_query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::cerr << "add_edges() Failed to execute" << PQerrorMessage(conn_) << std::endl;
        PQclear(res);

        return false;
    }

    PQclear(res);

    auto finish2{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed2{finish2 - start_timer};
    std::cout << "add_edges() took: " << time_elapsed2.count() << " seconds" << std::endl;

    return true;
}

bool DBfunctions::add_edges_between_segments_and_submunicipality(const db_result_t& data, const std::string dk_mun_key){
    auto start_timer{std::chrono::steady_clock::now()};

    std::string start = std::format("SELECT * FROM cypher('{}',$$UNWIND [ ", graph_name_);
    std::string end = std::format("] AS seg_id MATCH(a:sub_municipality {{dk_municipalitykey:'{}'}}), (b:Segment {{id:seg_id}}) CREATE (a)-[:contains]->(b) $$) AS (e agtype);", dk_mun_key);
    std::string middle = "";
    for(const auto& row : data){
        middle += std::format("{},", row);
    }
    
    if (!middle.empty()) middle.pop_back();
    
    std::string full_query = start + middle + end;

    PGresult *res = PQexec(conn_, full_query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::cerr << "add_edges_between_segments_and_submunicipality() Failed to execute" << PQerrorMessage(conn_) << std::endl;
        PQclear(res);

        return false;
    }
    PQclear(res);

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish - start_timer};
    std::cout << "add_edges() took: " << time_elapsed.count() << " seconds" << std::endl;
    return true;
}

#include "DBfunctions.hpp"
#include <unordered_map>
#include <map>
#include <set>
#include <sstream>
#include <format>
#include <iostream>
#include <fstream>

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
        //Declares search path for apache age
        res_ = PQexec(conn_, R"(set search_path = ag_catalog, "$user", public;)"); 
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
    PQclear(res_);  
    res_ = PQexec(conn_, std::format("SELECT * FROM ag_catalog.create_graph('{}');", graph_name_).c_str());   
    PQclear(res_);   
    return true;
}

bool DBfunctions::CreateMunicipalities()
{

    auto start_timer{std::chrono::steady_clock::now()};

    // TODO(NKC): implement error checking
    db_result_t municipalities = returnResult(conn_, "SELECT * FROM regions.dk_municipalities ORDER BY dk_municipalitykey");

    std::string current_city;
    std::string current_region;
    uint8_t sub_municipality_count = 1;
    std::cout << municipalities.size() << std::endl;

    for (const auto &sub_municipality : municipalities)
    {
        if (current_region.compare(sub_municipality.at(2)))
        {
            // std::cout << current_region << " is not " << sub_municipality.at(2) << std::endl;
            current_region = sub_municipality.at(2);
            PGresult *res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ MERGE (:region {{name: '{}', region_code: {}}}) $$) as (n agtype)", graph_name_, sub_municipality.at(4), sub_municipality.at(2)).c_str());
            PQclear(res);
        }
        if (current_city.compare(sub_municipality.at(1)))
        {
            sub_municipality_count = 1;
            // std::cout << current_city << " is not " << sub_municipality.at(1) << std::endl;
            current_city = sub_municipality.at(1);
            PGresult *res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ MERGE (:municipality {{name: '{}', code: {}}}) $$) as (n agtype)", graph_name_, sub_municipality.at(3), sub_municipality.at(1)).c_str());
            PGresult *res1 = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ MATCH (a:region), (b:municipality) WHERE a.region_code = {} AND b.code = {} CREATE (a)-[e:contains]->(b) RETURN e $$) as (e agtype)", graph_name_, sub_municipality.at(2), sub_municipality.at(1)).c_str());
            PQclear(res);
            PQclear(res1);
        }
        PGresult *res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ CREATE (:sub_municipality {{name: '{}-{}', dk_municipalitykey: {}}}) $$) as (n agtype)", graph_name_, sub_municipality_count, sub_municipality.at(3), sub_municipality.at(0)).c_str());
        PGresult *res1 = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ MATCH (a:municipality), (b:sub_municipality) WHERE a.code = {} AND b.dk_municipalitykey = {} CREATE (a)-[e:contains]->(b) RETURN e $$) as (e agtype)", graph_name_, sub_municipality.at(1), sub_municipality.at(0)).c_str());
        PQclear(res);
        PQclear(res1);

        sub_municipality_count += 1;
    }

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish - start_timer};
    std::cout << "CreateMunicipalities() took: " << time_elapsed.count() << " seconds" << std::endl;

    return true;
}

bool DBfunctions::CreateNodesForAllSubMunicipalities()
{

    auto start_timer{std::chrono::steady_clock::now()};

    db_result_t municipalities = returnResult(conn_, "SELECT dk_municipalitykey FROM regions.dk_municipalities ORDER BY dk_municipalitykey");
    std::map<std::string, Segment> segment_map;
    for (const auto &sub_municipality : municipalities)
    {
        std::string sub_municipality_string = sub_municipality.at(0);
        std::cout << sub_municipality_string << std::endl;
        db_result_t segments = returnResult(conn_, std::format("SELECT segmentkey, startpoint, endpoint, category, direction, name FROM maps.osm_dk_20140101 WHERE ST_Intersects(segmentgeo::geometry, (SELECT ST_Union(geog::geometry) FROM regions.dk_municipalities WHERE dk_municipalitykey in ({})));", sub_municipality.at(0)).c_str());
        for (const auto &segment : segments)
        {

            if (segment_map.contains(segment.at(0)))
            {
                segment_map[segment.at(0)].municipality_keys.emplace_back(sub_municipality_string);
            }
            else
            {
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

    // TODO(nkc): batch up to 2000 segments per database request
    for (const auto &[key, data] : segment_map)
    {
        if (it % 50 == 0)
        {
            std::cout << it << std::endl;
        }

        switch (data.municipality_keys.size())
        {
        case 1:
            PQclear(PQexec(conn_, std::format(
                                      "SELECT * FROM cypher('{}', $$ "
                                      "MATCH (a:sub_municipality) "
                                      "WHERE a.dk_municipalitykey = '{}' "
                                      "CREATE (a)-[:contains]->(:segment {{segmentkey: {}, startpoint: {}, endpoint: {}, category: '{}', direction: '{}', name: '{}' }}) "
                                      "$$) as (n agtype);",
                                      graph_name_,
                                      data.municipality_keys.at(0),
                                      data.segmentkey,
                                      data.startpoint,
                                      data.endpoint,
                                      data.category,
                                      data.direction,
                                      data.name)
                                      .c_str()));
            break;
        case 2:
            PQclear(PQexec(conn_, std::format(
                                      "SELECT * FROM cypher('{}', $$ "
                                      "MATCH (a:sub_municipality), (b:sub_municipality) "
                                      "WHERE a.dk_municipalitykey = {} AND b.dk_municipalitykey = {} "
                                      "CREATE (a)-[:contains]->(:segment {{segmentkey: {}, startpoint: {}, endpoint: {}, category: '{}', direction: '{}', name: '{}' }})"
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
                                      data.name)
                                      .c_str()));
            break;
        case 3:
            PQclear(PQexec(conn_, std::format(
                                      "SELECT * FROM cypher('{}', $$ "
                                      "MATCH (a:sub_municipality), (b:sub_municipality), (c:sub_municipality) "
                                      "WHERE a.dk_municipalitykey = {} AND b.dk_municipalitykey = {} AND c.dk_municipalitykey = '{}' "
                                      "CREATE (s:segment {{segmentkey: {}, startpoint: {}, endpoint: {}, category: '{}', direction: '{}', name: '{}' }}), "
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
                                      data.name)
                                      .c_str()));
            break;
        default:
            std::cerr << "Unhandled size " << data.municipality_keys.size() << std::endl;
            std::cout << "Key: " << key << ", subs: ";

            for (const auto &el : data.municipality_keys)
            {
                std::cout << el << " ";
            }
            std::cout << std::endl;
            break;
        }
        it += 1;
        /*
        if (data.size() > 2) {
            nicklas2_
        std::cout << "Key: " << key << ", subs: ";

        for (const auto& el : data) {
            std::cout << el << " ";
        }
        std::cout << std::endl;
        }
        */
    }
    
    auto finish2{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed2{finish2-start_timer};
    std::cout<< "CreateNodesForAllSubMunicipalities() took: " << time_elapsed2.count() <<" seconds" << std::endl;

    return true;
}

bool DBfunctions::CreateEdgesForAllSegments(){
    
    auto function_start_timer{std::chrono::steady_clock::now()};
    PQclear(PQexec(conn_, "BEGIN"));
    db_result_t municipalities = returnResult(conn_, "SELECT dk_municipalitykey FROM regions.dk_municipalities ORDER BY dk_municipalitykey");

    for(const auto& municipality : municipalities) {
        std::cout << municipality.at(0) << std::endl;
        
        auto start_timer2{std::chrono::steady_clock::now()};

        PGresult* resBB = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
        "MATCH (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(a:segment {{direction: 'BOTH'}}), (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(b:segment {{direction: 'BOTH'}}) "
        "WHERE a.segmentkey <> b.segmentkey and (a.startpoint = b.startpoint or a.startpoint = b.endpoint or a.endpoint = b.startpoint or a.endpoint = b.endpoint) "
        "CREATE (a)-[c:connected_to]->(b) "
        "RETURN properties(a), c, properties(b) $$) as (a agtype, c agtype, b agtype);", graph_name_, municipality.at(0), municipality.at(0)).c_str());
        
        if(PQresultStatus(resBB) != PGRES_TUPLES_OK){
            std::cerr << "Query for BOTH->BOTH Failed to execute " <<PQerrorMessage(conn_)<< std::endl;
            PQclear(resBB);

            PQclear(PQexec(conn_, "ROLLBACK"));
            return false;
        }

        auto query1{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed1{query1 - start_timer2};
        std::cout << "BOTH->BOTH took: " << time_elapsed1.count() << " seconds" << std::endl;

        PGresult* resBF = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
        "MATCH (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(a:segment {{direction: 'BOTH'}}), (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(b:segment {{direction: 'FORWARD'}}) "
        "WHERE a.startpoint = b.startpoint or a.endpoint = b.startpoint "
        "MERGE (a)-[c:connected_to]->(b) "
        "RETURN properties(a), c, properties(b) $$) as (a agtype, c agtype, b agtype);", graph_name_, municipality.at(0), municipality.at(0)).c_str());

        if (PQresultStatus(resBF) != PGRES_TUPLES_OK)
        {
            std::cerr << "Query for BOTH->FORWARD Failed to execute " << PQerrorMessage(conn_) << std::endl;
            PQclear(resBF);

            PQclear(PQexec(conn_, "ROLLBACK"));
            return false;
        }

        auto query2{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed2{query2 - query1};
        std::cout << "BOTH->FORWARD took: " << time_elapsed2.count() << " seconds" << std::endl;

        PGresult* resFB = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
        "MATCH (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(a:segment {{direction: 'FORWARD'}}), (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(b:segment {{direction: 'BOTH'}}) "
        "WHERE a.endpoint = b.startpoint or a.endpoint = b.endpoint "
        "MERGE (a)-[c:connected_to]->(b) "
        "RETURN properties(a), c, properties(b) $$) as (a agtype, c agtype, b agtype);", graph_name_, municipality.at(0), municipality.at(0)).c_str());
        
        if(PQresultStatus(resFB) != PGRES_TUPLES_OK){
            std::cerr << "Query for FORWARD->BOTH Failed to execute " <<PQerrorMessage(conn_)<< std::endl;
            PQclear(resFB);

            PQclear(PQexec(conn_, "ROLLBACK"));
            return false;
        }

        auto query3{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed3{query3 - query2};
        std::cout << "FORWARD->BOTH took: " << time_elapsed3.count() << " seconds" << std::endl;

        PGresult* resFF = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
        "MATCH (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(a:segment {{direction: 'FORWARD'}}), (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(b:segment {{direction: 'FORWARD'}}) "
        "WHERE a.endpoint = b.startpoint "
        "CREATE (a)-[c:connected_to]->(b) "
        "RETURN properties(a), c, properties(b) $$) as (a agtype, c agtype, b agtype);", graph_name_, municipality.at(0), municipality.at(0)).c_str());

        if (PQresultStatus(resFF) != PGRES_TUPLES_OK)
        {
            std::cerr << "Query for FORWARD->FORWARD Failed to execute " << PQerrorMessage(conn_) << std::endl;
            PQclear(resFF);

            PQclear(PQexec(conn_, "ROLLBACK"));
            return false;
        }

        auto query4{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed4{query4 - query3};
        std::cout << "FORWARD->FORWARD took: " << time_elapsed4.count() << " seconds" << std::endl;

        PQclear(resBB);
        PQclear(resBF);
        PQclear(resFB);
        PQclear(resFF);
    }

    PQclear(PQexec(conn_, "COMMIT"));

    auto finish2{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed2{finish2-function_start_timer};
    std::cout<< "CreateEdgesForAllSegments() took: " << time_elapsed2.count() <<" seconds" << std::endl;

    return true;
}

bool DBfunctions::BenchmarkQuery(std::string benchmark_name, const std::string query) {
    std::cout<< benchmark_name <<" running:" /*<< query*/ << std::endl;

    auto function_start_timer{std::chrono::steady_clock::now()};
    std::ofstream output;
    output.open (benchmark_name + ".csv");
    db_result_t res = returnResult(conn_, query.c_str());

    for (const auto& row : res) {
        for (const auto& col : row) {
            output << col << ",";
        }
        output << "\n";
    }
    output.close();

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish-function_start_timer};
    std::cout<< benchmark_name <<" took: " << time_elapsed.count() <<" seconds" << std::endl;
    std::cout<<std::endl;
    
    return true;
}

bool DBfunctions::CreateNodesForAllStartAndEndpoints()
{

    auto start_timer{std::chrono::steady_clock::now()};

    db_result_t municipalities = returnResult(conn_, "SELECT dk_municipalitykey FROM regions.dk_municipalities ORDER BY dk_municipalitykey");
    std::map<std::string, Segment> segment_map;
    for (const auto &sub_municipality : municipalities)
    {

        std::string sub_municipality_string = sub_municipality.at(0);
        std::cout << sub_municipality_string << std::endl;
        db_result_t segments = returnResult(conn_, std::format("select distinct point from (SELECT startpoint as point FROM experiments.data_for_graph WHERE ST_Intersects( segmentgeo::geometry, (SELECT ST_Union(geog::geometry) FROM regions.dk_municipalities WHERE dk_municipalitykey in ({}))) UNION ALL SELECT endpoint as point FROM experiments.data_for_graph WHERE ST_Intersects( segmentgeo::geometry, (SELECT ST_Union(geog::geometry) FROM regions.dk_municipalities WHERE dk_municipalitykey in ({}))));", sub_municipality.at(0), sub_municipality.at(0)).c_str());
        for (const auto &segment : segments)
        {

            if (segment_map.contains(segment.at(0)))
            {
                segment_map[segment.at(0)].municipality_keys.emplace_back(sub_municipality_string);
            }
            else
            {
                segment_map[segment.at(0)] = {};
                segment_map[segment.at(0)].point = segment.at(0);
                segment_map[segment.at(0)].municipality_keys.emplace_back(sub_municipality_string);
            }
        }
    }

    int it = 1;

    // TODO(nkc): batch up to 2000 segments per database request
    for (const auto &[key, data] : segment_map)
    {
        if (it % 50 == 0)
        {
            std::cout << it << std::endl;
        }

        switch (data.municipality_keys.size())
        {
        case 1:
            PQclear(PQexec(conn_, std::format(
                                      "SELECT * FROM cypher('{}', $$ "
                                      "MATCH (a:sub_municipality) "
                                      "WHERE a.dk_municipalitykey = {} "
                                      "CREATE (a)-[:contains]->(:intersection {{point: {} }}) "
                                      "$$) as (n agtype);",
                                      graph_name_,
                                      data.municipality_keys.at(0),
                                      data.point)
                                      .c_str()));
            break;
        case 2:
            PQclear(PQexec(conn_, std::format(
                                      "SELECT * FROM cypher('{}', $$ "
                                      "MATCH (a:sub_municipality), (b:sub_municipality) "
                                      "WHERE a.dk_municipalitykey = {} AND b.dk_municipalitykey = {} "
                                      "CREATE (a)-[:contains]->(:intersection {{point: {} }})"
                                      "<-[:contains]-(b) "
                                      "$$) as (n agtype);",
                                      graph_name_,
                                      data.municipality_keys.at(0),
                                      data.municipality_keys.at(1),
                                      data.point)
                                      .c_str()));
            break;
        case 3:
            PQclear(PQexec(conn_, std::format(
                                      "SELECT * FROM cypher('{}', $$ "
                                      "MATCH (a:sub_municipality), (b:sub_municipality), (c:sub_municipality) "
                                      "WHERE a.dk_municipalitykey = {} AND b.dk_municipalitykey = {} AND c.dk_municipalitykey = {} "
                                      "CREATE (s:intersection {{point: {} }}), "
                                      "(a)-[:contains]->(s), (b)-[:contains]->(s), (c)-[:contains]->(s) "
                                      "$$) as (n agtype);",
                                      graph_name_,
                                      data.municipality_keys.at(0),
                                      data.municipality_keys.at(1),
                                      data.municipality_keys.at(2),
                                      data.point)
                                      .c_str()));
            break;
        default:
            std::cerr << "Unhandled size " << data.municipality_keys.size() << std::endl;
            std::cout << "Key: " << key << ", subs: ";

            for (const auto &el : data.municipality_keys)
            {
                std::cout << el << " ";
            }
            std::cout << std::endl;
            break;
        }
        it += 1;
    }

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish - start_timer};
    std::cout << "CreateNodesForAllStartAndEndpoints() took: " << time_elapsed.count() << " seconds" << std::endl;

    return true;
}

bool DBfunctions::CreateEdgesForAllIntersections()
{
    std::string start = std::format("select * from cypher('{}',$$ unwind [", graph_name_);

    PQexec(conn_, "LOAD 'age';");
    PQexec(conn_, "SET search_path = ag_catalog, \"$user\", public;");
    std::string end;
    std::string middle;
    std::string query;
    PGresult *resB;
    PGresult *resF;
    auto start_timer{std::chrono::steady_clock::now()};

    PQclear(PQexec(conn_, "BEGIN"));

    for (int i = 1; i <= 311; i++)
    {
        std::cout << i << std::endl;

        auto start_timer2{std::chrono::steady_clock::now()};

        auto data = returnResult(conn_, std::format("select segmentkey, startpoint, endpoint, category, direction, name FROM experiments.data_for_graph WHERE ST_Intersects( segmentgeo::geometry, (SELECT ST_Union(geog::geometry) FROM regions.dk_municipalities WHERE dk_municipalitykey in ({}))) AND direction = 'BOTH'", i).c_str());

        middle = "";

        if (!data.empty())
        {

            for (size_t j = 0; j < data.size(); ++j)
            {

                std::string name = escape_quotes(data[j][5]);

                middle += std::format(
                    "{{ segmentkey: {}, startpoint: {}, endpoint: {}, category: '{}', direction: '{}', name: '{}' }},",
                    data[j][0],
                    data[j][1],
                    data[j][2],
                    data[j][3],
                    data[j][4],
                    name);
            }
            if (!middle.empty() && middle.back() == ',')
                middle.pop_back();

            end = std::format("] as row MATCH (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(a:intersection {{point:row.startpoint}}), (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(b:intersection {{point: row.endpoint}}) "
                              "CREATE (b)-[:segment {{segmentkey: row.segmentkey, category: row.category, direction: row.direction, name: row.name }}]->(a) "
                              "CREATE (a)-[:segment {{segmentkey: row.segmentkey, category: row.category, direction: row.direction, name: row.name }}]->(b) $$) as (n agtype);",
                              i, i);

            query = start + middle + end;
            resB = PQexec(conn_, query.c_str());

            if (PQresultStatus(resB) != PGRES_TUPLES_OK)
            {
                std::cerr << "Query for BOTH Failed to execute " << PQerrorMessage(conn_) << std::endl;
                PQclear(resB);

                PQclear(PQexec(conn_, "ROLLBACK"));
                return false;
            }
            PQclear(resB);

        }

        auto query1{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed1{query1 - start_timer2};
        std::cout << "BOTH took: " << time_elapsed1.count() << " seconds" << std::endl;

        data = returnResult(conn_, std::format("select segmentkey, startpoint, endpoint, category, direction, name FROM experiments.data_for_graph WHERE ST_Intersects( segmentgeo::geometry, (SELECT ST_Union(geog::geometry) FROM regions.dk_municipalities WHERE dk_municipalitykey in ({}))) AND direction = 'FORWARD'", i).c_str());

        middle = "";

        if (!data.empty())
        {

            for (size_t j = 0; j < data.size(); ++j)
            {
                std::string name = escape_quotes(data[j][5]);

                middle += std::format(
                    "{{ segmentkey: {}, startpoint: {}, endpoint: {}, category: '{}', direction: '{}', name: '{}' }},",
                    data[j][0],
                    data[j][1],
                    data[j][2],
                    data[j][3],
                    data[j][4],
                    name);
            }

            if (!middle.empty() && middle.back() == ',')
                middle.pop_back();

            end = std::format("] as row MATCH (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(a:intersection {{point:row.startpoint}}), (su:sub_municipality {{dk_municipalitykey: {}}})-[:contains]->(b:intersection {{point: row.endpoint}}) "
                              "CREATE (a)-[:segment {{segmentkey: row.segmentkey, category: row.category, direction: row.direction, name: row.name }}]->(b) $$) as (n agtype);",
                              i, i);

            query = start + middle + end;

            resF = PQexec(conn_, query.c_str());

            if (PQresultStatus(resF) != PGRES_TUPLES_OK)
            {
                std::cerr << "Query for FORWARD Failed to execute " << PQerrorMessage(conn_) << std::endl;
                PQclear(resF);

                PQclear(PQexec(conn_, "ROLLBACK"));
                return false;
            }
        PQclear(resF);
        }

        auto query2{std::chrono::steady_clock::now()};
        std::chrono::duration<double> time_elapsed2{query2 - query1};
        std::cout << "FORWARD took: " << time_elapsed2.count() << " seconds" << std::endl;

    }

    PQclear(PQexec(conn_, "COMMIT"));

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish - start_timer};
    std::cout << "CreateEdgesForAllIntersections() took: " << time_elapsed.count() << " seconds" << std::endl;

    return true;
}

// function that returns data based on the query it gets.
db_result_t returnResult(PGconn *conn, const char *query)
{

    // Result variable
    db_result_t result;

    // Checks if the query went through otherwise gives an error.
    PGresult *res = PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK &&
    PQresultStatus(res) != PGRES_COMMAND_OK)
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

// Extracts the data from csv file and returns the result as a matrix[rows X cols] of type strings.
db_result_t extract_Data_From_CSV(const std::string &filepath)
{
    auto start_timer{std::chrono::steady_clock::now()};

    // var declaration.
    db_result_t result;
    std::ifstream file(filepath);

    // checks of file is open, otherwose throws error.
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return result;
    }

    std::string line;

    // skips first line
    std::getline(file, line);

    // goes though the first 10 lines for simplicity
    // TODO(RBN): Make it go through all lines.
    // for(int i = 0; i < 10; i++)
    while (std::getline(file, line)) // FOR WHILE-LOOP
    {
        // std::getline(file,line); USE FOR FOR-LOOP
        std::vector<std::string> row;
        std::stringstream ss(line);
        std::string cell;

        // loops through the cells in the current row.
        int j = 0;
        while (std::getline(ss, cell, ','))
        {
            // checks if the current cell is data we want to extract.
            // segmentkey=0, startpoint=1, endpoint=2, category=5, direction=6, streetname= 14;
            if (j == 0 || j == 1 || j == 2 || j == 5 || j == 6 || j == 14)
            {
                row.push_back(cell);
            }
            j++;
        }
        ss.str("");
        ss.clear();
        result.push_back(row);
    }

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish - start_timer};

    std::cout << "Extract_Data_From_CSV() took: " << time_elapsed.count() << " seconds" << std::endl;

    // closes file and returns result as a matrix of strings
    file.close();
    result.shrink_to_fit();
    return result;
}

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

// takes data extracted from csv and insert into apache age.
bool insert_segments_as_nodes(PGconn *conn, const db_result_t &data)
{
    auto start_timer{std::chrono::steady_clock::now()};
    const size_t BATCH_SIZE = 1000;
    // var declarations
    std::string start = "select * from cypher('dummy_graph',$$ unwind [";

    std::string end = "] as row MERGE (s:Segment {id: row.id}) "
                      "SET s.start_point = row.start_point, "
                      "s.end_point = row.end_point, s.category = row.category, s.direction = row.direction, s.name = row.name $$) as (n agtype);";

    // takes the extracted CSV data and makes a node for every row in the matrix.

    for (size_t i = 0; i < data.size(); i += BATCH_SIZE)
    {
        std::string middle = "";
        for (size_t j = i; j < i + BATCH_SIZE && j < data.size(); ++j)
        {
            middle += "{id:" + data[j][0] + ", start_point: " + data[j][1] + ", end_point: " + data[j][2] + ", category: " + data[j][3] + ", direction: " + data[j][4] + ", name: " + data[j][5] + "},";
        }
        if (!middle.empty())
            middle.pop_back();
        std::string query = start + middle + end;
        PGresult *res = PQexec(conn, query.c_str());
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            std::cerr << "insert_segements_as_nodes() failed: " << PQerrorMessage(conn) << std::endl;
            PQclear(res);
            return false;
        }
        PQclear(res);
    }

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish - start_timer};

    std::cout << "insert_segments_as_nodes() took: " << time_elapsed.count() << " seconds" << std::endl;

    return true;
}

// adds edges between nodes where either start_point or end_point overlaps. Does not take direction into consideration.
// TODO(RBN): make Check for directions.
bool add_edges(PGconn *conn, db_result_t &data)
{

    auto start_timer{std::chrono::steady_clock::now()};

    std::string start = "SELECT * FROM cypher('dummy_graph',$$UNWIND [ ";
    std::string end = "] AS pair "
                      "MATCH (a:Segment {id: pair[0]}), (b:Segment {id: pair[1]}) "
                      "MERGE (a)-[:CONNECTED_TO]->(b) "
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

    PQexec(conn, "BEGIN");

    // Executes query and stores result.
    PGresult *res = PQexec(conn, fill_query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::cerr << "add_edges() Failed to execute" << PQerrorMessage(conn) << std::endl;
        PQclear(res);

        PQexec(conn, "ROLLBACK");
        return false;
    }

    PQclear(res);
    PQexec(conn, "COMMIT");

    auto finish2{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed2{finish2 - start_timer};
    std::cout << "add_edges() took: " << time_elapsed2.count() << " seconds" << std::endl;

    return true;
}

bool insert_segments_as_edges(PGconn *conn, const db_result_t &data, const std::string graph_name)
{
    auto start_timer{std::chrono::steady_clock::now()};
    const size_t BATCH_SIZE = 3000;
    // var declarations
    std::string start = std::format("select * from cypher('{}',$$ unwind [", graph_name);

    std::string end = "] as row MERGE (s:intersection {id: row.id}) $$) as (n agtype);";

    for (size_t i = 0; i < data.size(); i += BATCH_SIZE)
    {
        std::string middle = "";
        for (size_t j = i; j < i + BATCH_SIZE && j < data.size(); ++j)
        {
            middle += std::format("{{id: {} }},", data[j][0]);
        }

        if (!middle.empty())
            middle.pop_back();
        std::string query = start + middle + end;
        PGresult *res = PQexec(conn, query.c_str());
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            std::cerr << "insert_segements_as_edges() failed: " << PQerrorMessage(conn) << std::endl;
            PQclear(res);
            return false;
        }
        PQclear(res);
    }

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish - start_timer};

    std::cout << "insert_segments_as_edges() took: " << time_elapsed.count() << " seconds" << std::endl;

    return true;
}

//Extracts the data from csv file and returns the result as a matrix[rows X cols] of type strings.
std::vector<Trip_row> extract_Trip_Data_From_CSV(const std::string& filepath){
    auto start_timer{std::chrono::steady_clock::now()};

    //var declaration.
    std::vector<Trip_row> result;
    std::ifstream file(filepath);

    //checks of file is open, otherwose throws error.
    if(!file.is_open()){
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return result; 
    }

    std::string line;

    //skips first line
    std::getline(file,line);

    while(std::getline(file,line))
    {
        Trip_row row;
        std::vector<std::string> temp_row;
        std::stringstream ss(line);
        std::string part1;
        std::string part2;
        std::string part3;
        
        std::getline(ss,part1,'{');
        std::getline(ss,part2,'}');
        std::getline(ss,part3);
        
        std::stringstream ssp1(part1);
        std::stringstream ssp3(part3);
        std::string cell;

        //loops through the cells in the current row.
        while(std::getline(ssp1,cell,','))
        {
            // std::cout << cell << std::endl;
            if(cell != "\"") temp_row.push_back(cell);
        }
        
        // temp_row.push_back("{"+part2+"}");
        temp_row.push_back(part2);
        
        while(std::getline(ssp3,cell,','))
        {
            // std::cout << cell << std::endl;
            if(cell != "\"") temp_row.push_back(cell);
        }

        temp_row[4].erase(temp_row[4].find_last_not_of(" \n\r\t") + 1);
        
        for(int i=0; i<5; i++){
            if(temp_row[i].front() == '"'){
                temp_row[i].erase(temp_row[i].begin());
            }
            if(temp_row[i].back() == '"'){
                temp_row[i].erase(temp_row[i].end()-1);
            }
        }
        
        row.trip_id = std::stoi(temp_row[0]);
        row.segment_amount = std::stoi(temp_row[1]);
        std::stringstream ssarr(temp_row[2]);
        while(std::getline(ssarr,cell,','))
        {
            row.segment_array.push_back(std::stoi(cell));
        }
        row.total_meters_driven = std::stod(temp_row[3]);
        row.geo_trip = temp_row[4];

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

bool insert_trips_as_nodes(PGconn* conn, const std::vector<Trip_row>& data){
    auto start_timer{std::chrono::steady_clock::now()};
    const size_t BATCH_SIZE = 1000;
    //var declarations
    std::string start = "select * from cypher('chr_dummy_graph',$$ unwind [";

    std::string end = "] as row MERGE (t:Trip {id: row.trip_id}) "
                      "SET t.segment_amount = row.segment_amount, "
                      "t.segment_array = row.segment_array, "
                      "t.total_meters_driven = row.total_meters_driven"//, "
                    //   "t.geo_trip = row.geo_trip "
                    "$$) as (n agtype);";

    //takes the extracted CSV data and makes a node for every row in the matrix.

    for (size_t i = 0; i<data.size(); i+=BATCH_SIZE){
        std::string middle="";
        for (size_t j = i; j < i+BATCH_SIZE && j<data.size(); ++j)
        {
            std::string arr_to_string="\"{";
            for (int arr_size = 0; arr_size<data[j].segment_array.size(); arr_size++){
                arr_to_string += std::to_string(data[j].segment_array[arr_size]) + ",";
            }
            if (!arr_to_string.empty()) {
                arr_to_string.pop_back();
            }
            arr_to_string += "}\"";
            
            // middle += "{trip_id:" + data[j][0] + ", segment_amount: " + data[j][1] + ", segment_array: " + data[j][2] + ", total_meters_driven: " + data[j][3] + ", geo_trip: " + data[j][4] +  "},";
            middle += "{trip_id: " + std::to_string(data[j].trip_id) + ", segment_amount: " + std::to_string(data[j].segment_amount) + ", segment_array: " + arr_to_string + ", total_meters_driven: " + std::to_string(data[j].total_meters_driven) + "},";
        }
        if (!middle.empty()) middle.pop_back();
        std::string query = start + middle + end;
        PGresult* res= PQexec(conn, query.c_str());
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::cerr << "insert_trips_as_nodes() failed: " << PQerrorMessage(conn) << std::endl;
            PQclear(res);
            return false;
        }
        PQclear(res);
    }

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish-start_timer};

    std::cout<< "insert_trips_as_nodes() took: " << time_elapsed.count() <<" seconds" << std::endl;
    
    return true;
}


std::string DBfunctions::escape_quotes(std::string s)
{
    size_t pos = 0;
    while ((pos = s.find('\'', pos)) != std::string::npos)
    {
        s.replace(pos, 1, "''");
        pos += 2;
    }
    return s;
}

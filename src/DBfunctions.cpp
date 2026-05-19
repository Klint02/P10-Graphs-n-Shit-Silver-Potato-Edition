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
        PQclear(PQexec(conn_, R"(LOAD 'age';)"));
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

        if(PQresultStatus(resBF) != PGRES_TUPLES_OK){
            std::cerr << "Query for BOTH->FORWARD Failed to execute " <<PQerrorMessage(conn_)<< std::endl;
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

        std::cout<<"datasize: "<<data.size()<<std::endl;
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
            // std::cout<<query<<std::endl;
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

        std::cout<<"datasize: "<<data.size()<<std::endl;

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


bool DBfunctions::CreateTrajectories() 
{
    auto start_timer{std::chrono::steady_clock::now()};
    
    //TODO(CLM): Find a faster way to include geo_trip. Current way (commented out) is 5 times slower due to inner join.
    db_result_t trajectories = returnResult(conn_, "SELECT trip_id, count(trip_id) as segment_amount, ARRAY_AGG(segmentkey) as segment_array, sum(meters_driven) as total_meters_driven, sum(seconds) as total_trajectory_duration, ARRAY_AGG(seconds) as segment_duration "
                                            "FROM mapmatched_data.viterbi_match_osm_dk_20140101 "
                                            "GROUP BY trip_id");
    // db_result_t trajectories = returnResult(conn_, "SELECT trip_id, count(trip_id) as segment_amount, ARRAY_AGG(trip.segmentkey) as segment_array, sum(meters_driven) as total_meters_driven, sum(seconds) as total_trajectory_duration, ARRAY_AGG(seconds) as segment_duration, ST_Union(segmentgeo::geometry) as geo_trajectory "
    //                                         "FROM(SELECT trip_id, segmentkey, meters_driven, seconds "
    //                                         "FROM mapmatched_data.viterbi_match_osm_dk_20140101) trip "
    //                                         "INNER JOIN (SELECT segmentkey, segmentgeo "
    //                                         "FROM maps.osm_dk_20140101) segmentmap "
    //                                         "ON trip.segmentkey = segmentmap.segmentkey "
    //                                         "GROUP BY trip_id");

    std::cout << trajectories.size() << std::endl;

    PQclear(PQexec(conn_, "BEGIN"));

    auto count = 0;
    for(auto trajectory : trajectories){
        if(count%1000==0) std::cout << count << std::endl;
        count++;

        PGresult* res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
            "CREATE (:trajectory {{id: '{}', segment_amount: '{}', segment_array: '{}', total_meters_driven: '{}', total_duration: '{}'}}) "
            "$$) as (n agtype)", graph_name_, trajectory.at(0), trajectory.at(1), trajectory.at(2), trajectory.at(3), trajectory.at(4)).c_str());
        // PGresult* res = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ CREATE (:trajectory {{id: '{}', segment_amount: '{}', segment_array: '{}', total_meters_driven: '{}', total_duration: '{}', geotrip: '{}'}}) $$) as (n agtype)", graph_name_, trajectory.at(0), trajectory.at(1), trajectory.at(2), trajectory.at(3), trajectory.at(4), trajectory.at(6)).c_str());
        
        if(PQresultStatus(res) != PGRES_TUPLES_OK){
            std::cerr << "CreateTrajectories() Failed to execute " <<PQerrorMessage(conn_)<< std::endl;
            PQclear(res);
            
            PQclear(PQexec(conn_, "ROLLBACK"));
            return false; 
        }
        
        PQclear(res);
    }

    PQclear(PQexec(conn_, "COMMIT"));

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish-start_timer};

    std::cout<< "It took CreateTrajectories() " << time_elapsed.count() <<" seconds to compute." << std::endl;
        
    return true;
}

bool DBfunctions::CreateTrajectoryConnectionForSAE() 
{
    auto start_timer{std::chrono::steady_clock::now()};
    
    
    PGresult* trajectory_result = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
    "MATCH (t:trajectory) "
    "RETURN t.id, id(t), t.segment_array "
    "ORDER BY (t) "
    "$$) AS (trajectory_id agtype, trajectory_vertex_id agtype, trajectory_segment_array agtype);", graph_name_).c_str());

    if(PQresultStatus(trajectory_result) != PGRES_TUPLES_OK){
        std::cerr << "Query for trajectories id, table location and segment array failed to execute. " << PQerrorMessage(conn_) << std::endl;
        PQclear(trajectory_result);
        return false; 
    }
    
    std::vector<int> trajectory_ids;
    std::unordered_map<int, long> trajectory_map_graph_id;
    std::unordered_map<int, std::vector<int>> trajectory_map_segment_array;
    int trajectory_row_count = PQntuples(trajectory_result);
    
    for(int t=0;t<trajectory_row_count;t++){
        std::string temp = PQgetvalue(trajectory_result, t, 0);
        if(temp.front() == '"'){
            temp.erase(temp.begin());
        }
        int trajectory_id = std::stoi(temp);
        trajectory_ids.push_back(trajectory_id);

        long trajectory_vertex_id = std::stol(PQgetvalue(trajectory_result, t, 1));
        trajectory_map_graph_id[trajectory_id] = trajectory_vertex_id;
    
        std::string temp2 = PQgetvalue(trajectory_result, t, 2);
        temp2.erase(temp2.begin());
        temp2.erase(temp2.begin());
        temp2.pop_back();
        temp2.pop_back();
        
        std::stringstream ss(temp2);
        std::string arr_split;
        std::vector<int> segment_array;
        while (std::getline(ss, arr_split, ',')){
            segment_array.push_back(std::stoi(arr_split));
        }
    
        trajectory_map_segment_array[trajectory_id] = segment_array;
    }

    PQclear(trajectory_result);

    auto trajectory_map_finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> trajectory_map_time_elapsed{trajectory_map_finish-start_timer};
    
    std::cout << "Trajectory map finish. It took " << trajectory_map_time_elapsed.count() <<" seconds to compute." << std::endl;
    


    PGresult* intersection_result = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
    "MATCH (i1)-[se:segment]->(i2) "
    "RETURN se.segmentkey, id(i1), id(i2) "
    "$$) AS (segmentkey agtype, intersection_vertex_id_1 agtype, intersection_vertex_id_2 agtype);", graph_name_).c_str());

    if(PQresultStatus(intersection_result) != PGRES_TUPLES_OK){
        std::cerr << "Query for segmentkey and intersection vertex id failed to execute. " << PQerrorMessage(conn_) << std::endl;
        PQclear(intersection_result);
        return false; 
    }

    std::unordered_map<int, std::vector<long>> intersection_map;

    int intersection_row_count = PQntuples(intersection_result);
    for (int i = 0; i < intersection_row_count; i++) {
        std::string temp = PQgetvalue(intersection_result, i, 0);
        if(temp.front() == '"'){
            temp.erase(temp.begin());
        }
        int segmentkey = std::stoi(temp);
        if(intersection_map.find(segmentkey)!=intersection_map.end()) continue;

        long intersection_vertex_id_1 = std::stol(PQgetvalue(intersection_result, i, 1));
        long intersection_vertex_id_2 = std::stol(PQgetvalue(intersection_result, i, 2));
        intersection_map[segmentkey] = {intersection_vertex_id_1,intersection_vertex_id_2};
    }

    PQclear(intersection_result);
    
    auto intersection_map_finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> intersection_map_time_elapsed{intersection_map_finish-trajectory_map_finish};
    
    std::cout << "Intersection map finish. It took " << intersection_map_time_elapsed.count() <<" seconds to compute." << std::endl;



    PGresult* municipality_result = PQexec(conn_, std::format("SELECT * FROM cypher('{}', $$ "
    "MATCH (sm:sub_municipality)-[:contains]->(i:intersection) "
    "RETURN id(i), sm.dk_municipalitykey "
    "$$) AS (intersection_vertex_id agtype, municipalitykey agtype);", graph_name_).c_str());
    
    if(PQresultStatus(municipality_result) != PGRES_TUPLES_OK){
        std::cerr << "Query for intersection vertex id and municipalitykey failed to execute. " << PQerrorMessage(conn_) << std::endl;
        PQclear(municipality_result);
        return false; 
    }

    std::unordered_map<int, int> sub_municipality_map;

    int municipality_row_count = PQntuples(municipality_result);
    for (int m = 0; m < municipality_row_count; m++) {
        long intersection_vertex_id = std::stol(PQgetvalue(municipality_result, m, 0));
        
        std::string temp = PQgetvalue(municipality_result, m, 1);
        if(temp.front() == '"'){
            temp.erase(temp.begin());
        }
        int sub_municipality_key = std::stoi(temp);
        sub_municipality_map[intersection_vertex_id] = sub_municipality_key;
    }

    PQclear(municipality_result);
    
    auto sub_municipality_map_finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> sub_municipality_map_time_elapsed{sub_municipality_map_finish-intersection_map_finish};
    
    std::cout << "Sub_municipality map finish. It took " << sub_municipality_map_time_elapsed.count() <<" seconds to compute." << std::endl;



    auto query_time_sum=0.0;
    const size_t BATCH_SIZE = 200;
    const size_t EDGE_CREATION_CUTOFF = 4000;
    for(int b = 0; b < trajectory_row_count; b+=BATCH_SIZE){
        std::string start = std::format("SELECT * FROM cypher('{}', $$ unwind [", graph_name_);
        std::string middle="";
        std::string end = "] AS row "
        "MATCH (t:trajectory) "
        "WHERE id(t) = row.trajectory_internal_id "
        "MATCH (su:sub_municipality {dk_municipalitykey: row.dk_municipalitykey})-[:contains]->(i:intersection) "
        "WHERE id(i) = row.intersection_internal_id "
        "CREATE (t)-[tc:trajectory_connection]->(i) "
        "SET tc.occurrence_array = row.occurrence_array "
        "$$) as (n agtype);";

        std::cout << "Traject id's from " << b << " to " << b+BATCH_SIZE << std::endl;

        auto number_of_connections = 0;

        for (int j = b; j < b+BATCH_SIZE && j<trajectory_row_count; j++) {
            auto segment_array = trajectory_map_segment_array[trajectory_ids.at(j)];
            std::unordered_map<long,std::vector<int>> intersection_occurrence_array;

            for(int segmentkey : segment_array){
                if(intersection_map.find(segmentkey) == intersection_map.end()) continue;

                long point1 = intersection_map[segmentkey][0];
                intersection_occurrence_array[point1].push_back(segmentkey);
                long point2 = intersection_map[segmentkey][1];
                intersection_occurrence_array[point2].push_back(segmentkey);
            }

            for (const auto& [intersection_key, occurrence_array] : intersection_occurrence_array){
                std::string occurrence_string = "[";
                for(int occurrence : occurrence_array){
                    occurrence_string += std::to_string(occurrence) + ",";
                }
                if (occurrence_string.back() == ',') occurrence_string.pop_back();
                occurrence_string += "]";
                
                middle+=std::format("{{trajectory_internal_id: {}, intersection_internal_id: {}, dk_municipalitykey: '{}', occurrence_array: {}}},", 
                    trajectory_map_graph_id[trajectory_ids.at(j)], 
                    intersection_key, 
                    sub_municipality_map[intersection_key], 
                    occurrence_string);
            }
            
            number_of_connections += segment_array.size()+1;
            if(number_of_connections>EDGE_CREATION_CUTOFF) {
                b=j+1-BATCH_SIZE;
                break;
            }
        }
        if (middle.back() == ',') middle.pop_back();

        std::string query = start + middle + end;

        std::cout << "Number of edges between trajectory and segment: " << number_of_connections << std::endl;
        
        auto start_query_time{std::chrono::steady_clock::now()};
        
        PGresult* res = PQexec(conn_, query.c_str());    
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::cerr << "CreateTrajectoryConnectionForSAE() failed: " << PQerrorMessage(conn_) << std::endl;
            PQclear(res);
            return false;
        }
        PQclear(res);
        
        auto finish_query_time{std::chrono::steady_clock::now()};
        std::chrono::duration<double> query_time_elapsed{finish_query_time-start_query_time};
    
        std::cout<< "It took query " << query_time_elapsed.count() <<" seconds to compute." << std::endl;
        query_time_sum+=query_time_elapsed.count();
        std::cout << "Seconds since begun: " << query_time_sum << std::endl;
    }

    auto finish{std::chrono::steady_clock::now()};
    std::chrono::duration<double> time_elapsed{finish-start_timer};

    std::cout<< "It took CreateEdgesForTrajectoriesToSegments() " << time_elapsed.count() <<" seconds to compute." << std::endl;
        
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

/**
 * @file file.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef _SUBSCRIBER_FILE_H_
#define _SUBSCRIBER_FILE_H_
#include "ara/com/proxy_skeleton/definitions.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string.h>
#include <string>
#include <vector>

class Subscriber {
public:
  void write(std::string const& file_name,
             const ara::com::proxy_skeleton::Client_udp_Info &client) const {
    // Check if the data already exists
    if (check(file_name.c_str(), client)) {
      return;
    }

    // Open file for writing, append data
    std::ofstream sub_file(file_name.c_str(), std::ios::app);

    // If opened, write the data into the file
    if (sub_file.is_open()) {
      sub_file << client.port << "," << client.addr << "\n";
    } else {
      throw std::invalid_argument("Error opening file for writing, file name: " + file_name);
    }
  }

  // Check before writing in file -> No repeated data
  bool check(std::string const& file_name,
             const ara::com::proxy_skeleton::Client_udp_Info &client) const {
    // Open file for reading
    std::ifstream sub_file(file_name.c_str());

    // If opened, check for repeated data
    if (sub_file.is_open()) {
      std::string line;
      while (std::getline(sub_file, line)) {
        auto comma_pos = line.find(',');
        if (comma_pos != std::string::npos) {
          int port = std::stoi(line.substr(0, comma_pos));
          std::string addr = line.substr(comma_pos + 1);
          if (port == client.port && addr == client.addr) {
            return true;
          }
        }
      }
    } else {
      throw std::invalid_argument("Error opening file for reading, file name: " + file_name);
    }

    return false;
  }

  std::vector<ara::com::proxy_skeleton::Client_udp_Info>
  getrows(std::string const& file_name) const {
    std::vector<ara::com::proxy_skeleton::Client_udp_Info> vclients;

    // Open file for reading
    std::ifstream sub_file(file_name.c_str());

    // If opened, read the data into the vector
    if (sub_file.is_open()) {
      std::string line;
      while (std::getline(sub_file, line)) {
        auto comma_pos = line.find(',');
        if (comma_pos != std::string::npos) {
          auto port = (uint16_t)std::stoi(line.substr(0, comma_pos));
          std::string addr = line.substr(comma_pos + 1);
          vclients.push_back({port, addr});
        }
      }
    } else {
      throw std::invalid_argument("Error opening file for reading, file name: " + file_name);
    }

    return vclients;
  }

  // Server sendes stop offer service
  void delete_record(std::string const &file_name,
                     ara::com::proxy_skeleton::Client_udp_Info const& client) const {
    // Open the input file for reading
    std::ifstream infile(file_name);
    if (!infile.is_open()) {
      std::cerr << "Error: Could not open file " << file_name
                << " for reading.\n";
      return;
    }

    // Open the output file for writing
    std::ofstream outfile("temp.sub");
    if (!outfile.is_open()) {
      std::cerr << "Error: Could not create temporary file for writing.\n";
      return;
    }

    // Loop through each line of the input file
    std::string line;
    while (std::getline(infile, line)) {
      /// Split the line into port and addr
      std::istringstream iss(line);
      int port;
      std::string addr;
      if (!(iss >> port >> addr)) {
        std::cerr << "Error: Invalid line in input file: " << line << "\n";
        continue;
      }

      // Check if this line matches the client to delete
      if (port == client.port && addr == client.addr) {
        continue; // Skip this line
      }

      // Write the line to the output file
      outfile << line << "\n";
    }

    // Close the files
    infile.close();
    outfile.close();

    // Replace the input file with the output file
    if (std::remove(file_name.c_str()) != 0) {
      std::cerr << "Error: Could not delete file " << file_name << "\n";
      return;
    }
    if (std::rename("temp.sub", file_name.c_str()) != 0) {
      std::cerr << "Error: Could not rename temporary file to " << file_name
                << "\n";
      return;
    }
  }

  void clear(std::string const &file_name) const {
    // open file and clear its contents
    std::ofstream ofs;
    ofs.open(file_name, std::ofstream::out | std::ofstream::trunc);
    ofs.close();
  }

  void delete_file(std::string const &file_name) const {
    // delete file
    if (std::remove(file_name.c_str()) != 0) {
      std::cerr << "Error: Could not delete file " << file_name << "\n";
      return;
    }
  }
};

#endif // _SUBSCRIBER_FILE_H_
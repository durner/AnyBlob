import csv
import os

def read_csv(input_file, columns_to_include, vendor):
    output_data = []

    with open(input_file, 'r') as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            selected_data = [row[column] for column in columns_to_include]
            # Name
            selected_data[0] = "\"" + selected_data[0] + "\""
            # Memory
            try:
                selected_data[1] = float(''.join(c if c.isdigit() or c == '.' else '' for c in selected_data[1]))
            except ValueError:
                selected_data[1] = 0
            # vCPU
            try:
                selected_data[2] = int(float(''.join(c if c.isdigit() or c == '.' else '' for c in selected_data[2][:4])))
            except ValueError:
                selected_data[2] = 0
            # Network
            try:
                selected_data[3] = int(float(''.join(c if c.isdigit() or c == '.' else '' for c in selected_data[3])))
            except ValueError:
                selected_data[3] = 0

            # Specifics
            if vendor != "Azure":
                selected_data[3] *= 1000

            if vendor == "GCP":
                try:
                    selected_data[4] = int(float(''.join(c if c.isdigit() or c == '.' else '' for c in selected_data[4])))
                except ValueError:
                    selected_data[4] = 0
                if (selected_data[4] > selected_data[3]):
                    selected_data[3] = selected_data[4]
                selected_data.pop()

            # Network default
            if selected_data[3] == 0:
                selected_data[3] = 1000

            output_data.append(selected_data)
    return output_data

def write_to_file(output_data, output_file):
    with open(output_file, 'a') as f:
        f.write("{\n")
        for idx, data in enumerate(output_data):
            f.write("{ ")
            for lidx, value in enumerate(data):
                f.write(str(value))
                if lidx < len(data) - 1:
                    f.write(", ")
                else:
                    f.write(" }")
            if idx < len(output_data) - 1:
                f.write(",\n")
            else:
                f.write(" };\n")

def write_header(vendor, output_file):
    with open(output_file, 'w') as f:
        f.write("#include \"cloud/" + vendor.lower() + "_instances.hpp\"\n")
        f.write("//---------------------------------------------------------------------------\n")
        f.write("// AnyBlob - Universal Cloud Object Storage Library\n")
        f.write("// Dominik Durner, 2023\n")
        f.write("//\n")
        f.write("// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.\n")
        f.write("// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.\n")
        f.write("// SPDX-License-Identifier: MPL-2.0\n")
        f.write("//---------------------------------------------------------------------------\n")
        f.write("namespace anyblob::cloud {\n")
        f.write("//---------------------------------------------------------------------------\n")
        f.write("using namespace std;\n")
        f.write("//---------------------------------------------------------------------------\n")
        f.write("vector<" + vendor + "Instance> " + vendor + "Instance::getInstanceDetails()\n")
        f.write("// Gets a vector of instance type infos\n")
        f.write("{\n")
        f.write("    vector<" + vendor + "Instance> instances =\n")

def write_footer(output_file):
    with open(output_file, 'a') as f:
        f.write("return instances;\n")
        f.write("}\n")
        f.write("//---------------------------------------------------------------------------\n")
        f.write("} // namespace anyblob::cloud\n")

def main():
    vendors = ["AWS", "Azure", "GCP"]
    columns = {
        "AWS": ["API Name", "Instance Memory", "vCPUs", "Network Performance"],
        "Azure": ["Size", "Memory", "vC", "Network Bandwidth"],
        "GCP": ["name", "memoryGiB", "vCpus", "bandwidth", "tier1"],
    }
    for vendor in vendors:
        output_data = read_csv(vendor.lower()+".csv", columns.get(vendor), vendor)
        output_file = "../src/cloud/" + vendor.lower() + '_instances.cpp'
        write_header(vendor, output_file)
        write_to_file(output_data, output_file)
        write_footer(output_file)
        os.system("clang-format -i " + output_file)

if __name__ == "__main__":
    main()

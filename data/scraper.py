import requests
from bs4 import BeautifulSoup
import csv

def remove_sup_tags(cell):
    for sup_tag in cell.find_all('sup'):
        print(sup_tag.decompose())
    return cell.text.strip()

def remove_nics(cell):
    res = cell.split('/')
    if len(res) > 1:
        return res[len(res) - 1]
    else:
        return cell

def scrape_and_write_to_csv(url_list, csv_filename):
    required_headers = ["Size", "vC", "Memory", "Network Bandwidth"]

    with open(csv_filename, 'w', newline='', encoding='utf-8') as csv_file:
        writer = csv.writer(csv_file)

        writer.writerow(required_headers)

        for url in url_list:
            response = requests.get(url)

            if response.status_code == 200:
                soup = BeautifulSoup(response.text, 'html.parser')

                tables = soup.find_all('table')

                for table in tables:
                    rows = table.select('tbody tr')

                    headers = [header.text.strip() for header in table.select('thead th')]

                    headers = []
                    for header in table.select('thead th'):
                        added = False
                        for rheader in required_headers:
                            if rheader.lower() in header.text.strip().lower() and rheader != "Size":
                                headers.append(rheader)
                                added = True
                            elif rheader.lower() == header.text.strip().lower():
                                headers.append(rheader)
                                added = True
                        if not added:
                            headers.append(header.text.strip())

                    print(headers)
                    if all(header in headers for header in required_headers):
                        for row in rows:
                            row_data = [remove_nics(remove_sup_tags(cell)) for cell in row.find_all('td')]
                            row_data_filtered = [row_data[headers.index(header)] for header in required_headers]
                            writer.writerow(row_data_filtered)
            else:
                print(f"Failed {url}. Status code: {response.status_code}")


if __name__ == "__main__":
    # Azure website URLs
    urls = ["https://learn.microsoft.com/en-us/azure/virtual-machines/av2-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/sizes-b-series-burstable",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/bsv2-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/basv2",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/bpsv2-arm",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/dcv2-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/dcv3-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/dv2-dsv2-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/dv3-dsv3-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/dv5-dsv5-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/ddv5-ddsv5-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/dasv5-dadsv5-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/dcasv5-dcadsv5-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/dpsv5-dpdsv5-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/dplsv5-dpldsv5-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/dlsv5-dldsv5-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/dcesv5-dcedsv5-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/dav4-dasv4-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/dv4-dsv4-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/ddv4-ddsv4-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/fsv2-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/fx-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/dv2-dsv2-series-memory", "https://learn.microsoft.com/en-us/azure/virtual-machines/ev3-esv3-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/eav4-easv4-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/edv4-edsv4-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/ev4-esv4-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/ev5-esv5-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/ebdsv5-ebsv5-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/edv5-edsv5-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/easv5-eadsv5-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/ecasv5-ecadsv5-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/ecasccv5-ecadsccv5-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/ecesv5-ecedsv5-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/epsv5-epdsv5-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/m-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/msv2-mdsv2-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/msv3-mdsv3-medium-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/mv2-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/lsv2-series", "https://learn.microsoft.com/en-us/azure/virtual-machines/lsv3-series",
            "https://learn.microsoft.com/en-us/azure/virtual-machines/lasv3-series"]  # Add your URLs here

    output_csv_filename = "azure.csv"

    scrape_and_write_to_csv(urls, output_csv_filename)

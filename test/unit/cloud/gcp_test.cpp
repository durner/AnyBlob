#include "cloud/gcp.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/gcp_instances.hpp"
#include "cloud/gcp_signer.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace cloud {
namespace test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TEST_CASE("gcp") {
    Provider::testEnviornment = true;

    char filename[] = "/tmp/tmp.gcp.key";
    {
        ofstream outfile(filename);
        string key = "-----BEGIN RSA PRIVATE KEY-----\n";
        key += "MIIG5AIBAAKCAYEAwXO93Bw1bBixG2rcm3cPAv7gSXWk584wnlH08iwL7jCdzT2H\n";
        key += "+6pkPWgHnG/bvlzSNoJg9B21axTica5zbman/tM/1MmLt/oXtYQL/Tbz9OL9PwIB\n";
        key += "ogKzCgvMrzTWLKn45thLLD8GW+00XFJumbe1oPQ42/PVSy5EB2Qnxfu4kBuxPgSO\n";
        key += "VwwXOp5i8KRlt3WDBcj82tM6d5bXo0ajhwSAOKWY3HMzhlSOCEQAEwo0kAqd2KXi\n";
        key += "ZtUjkr8KWUbuZEuNW/hqBIyeYO3SWLCB7Bl+aOtLnYnfJJ+lD7m3F7EH0CnZNOtE\n";
        key += "zBEtaDaMLRR9iN9XGZZOjd4yqgpJFTS6xTzXMEtS7fTWKbQarot8dDbHzTEqxdBP\n";
        key += "W8OovvoJDMXQ8JSt60m8o3666BnJOVA5j2bM8dUWaqCw5/QzFd7KU6Y2izL1Bplz\n";
        key += "UZ6dmDFsIwfyEk2FJ2zrkDZLdIeS0nt4CNaSinORWp2x3RbJ1kSUQiFli172BU9U\n";
        key += "B0wHfT7lWxqy0JPnAgMBAAECggGAErN9LauecHo8mNFuTSsnzNrV1NQmInCY251B\n";
        key += "qC2g46BYiPBDVo1kzljhr3pSzGbNvY5CECdVE+p7b5D1QL10zRof6BKpypnHM/l1\n";
        key += "bT+kOs68u5wWi1Jme3iji2Z4s+2kjbBoJ/lZxIY8UxZ28a5ERTCG1KCQRInO8sX4\n";
        key += "YFfSwj+jFUM8fWbCUhzgpLHH0YHvLh59bywPUWNIKlUYVuOU/6Tmj50lZH2FE4B7\n";
        key += "X1vTs/8Kvlhe+CDvpO6J8hJ81j5FWyuCy+yj8Oakspz6rvnKAFKdPt+czu6tK64w\n";
        key += "+YpNObEigrEchnElL3aIbSGM921xPXJkFXm9Wk4yKUBF39DtpM8VN13mYoNvkOE7\n";
        key += "K+mHUZ2ZV4ph2tIVAu9pRQptg810hloFywBVOKDfUeB0DbgOU122QTJAmIBDOfwf\n";
        key += "7P5ouymRSXtGZYuKAEiSTiwucQmLzbYvWt63REsiMWW2QRYb6DkpslI1G23ZIPDD\n";
        key += "epMAS6t0DCFbX0b/sVahUtmqARj1AoHBAMJCJ11WxLGcWxFovPqAyDONdagLnRbz\n";
        key += "s1AG382aFRjzzMWmQ66j675tLBNFZBd9PfMFvyTosKXguhrBLCzW2ceESesPG5sT\n";
        key += "4PoOMAEADReqImumlTU5vzWfOTsWrrxIWKrQBKvFS1qVkz31w7Lcdg6a7PsIyl/h\n";
        key += "vda2ysJljYJoH2pERsA1eLAwSd+l+FYGIAEylZiMoCgFekvVeTNzOe7U/zArTpE/\n";
        key += "Tg6MHrLTgy0x2GdPO0lvJvbQAbmQMXczjQKBwQD+7/vC5FBhIHfanj++PXP0z7NZ\n";
        key += "xPvIjkLU6wAxP4vwrQT8jGpVTZegD1Mkx81L3meBNogaB9bo91/Dvh+c/hrBa/n7\n";
        key += "W1LlWNfD7ubHmKBwRdZXtZSvUdqgJcTe46kntmiyDuqd/rmkGa3mgyxqFR3BQ24x\n";
        key += "SP5cjypsN2B6tkbSWgoohej+g0koogL6aGVKaWynl5Ki/I8dS96uiMEzUfilwkD3\n";
        key += "1zTxkuPRLiDaQq0vX+YYIpWIsS2HFjqrQmy17kMCgcEAnxGOrGbdv9aZ1+KdRL9p\n";
        key += "sJU4b/e6lc3O2kwWvYRbnEgfOQXRzFLcOt2oxsr+kCF1NehRwgZsiBhCLKBb7Qet\n";
        key += "4yuXX8zKPS2E/x3Y/yisj+E4OFB7Q89anK0aLyF+yhyvxod6G7H951ot7QGvU2ol\n";
        key += "ngYM4e8r0GHIkuaxl4eS3eMnPlxUVxYyEowoIeQFO2Pelzx1tSoKB1uc7jYK/i9v\n";
        key += "k/uET0xXFKby4wSoKqT6eGqlmssNcC99h4OCthG1/7cNAoHBAJ9vQkoeM06q2Yn1\n";
        key += "kfPNxukBxC6ODNDed1llJpemIESCUC4JOq7iecL2Eo9cDT43dw/OJMvyvyqvGkr2\n";
        key += "ahrqp5zzhED4Wh1otHeqvtVw1FWit2ve+X+zd0DUngyu4Ckf4NYKkhwBI+RG0wTo\n";
        key += "YCxvzE4Dd7SG69zDBErtTv8vY5dGDDhPlukk/enVeHtWMpKY4ATnvCMGRBKUUk1g\n";
        key += "5ULNLu2rUKovAsNZk/RMHuug62JHXUUWy+HSvKBQ/JwCTK5ytQKBwHJURIdf0huo\n";
        key += "XpCUbJ3RdeUoEynMlFrIDmQdd0sDNzP1NdUog+lxE2TziTq9+eob/EnDE/QI1OMb\n";
        key += "T87Ns8QjBkff/DDANFRqpYHxDzppYgOtDiIh/3bnaXJkVzcnSYyeyWfgCA/3NOBo\n";
        key += "HNt/F1aw+Rjvm9Lif9SI3xJ1qA97ClzQd8brBiR+r8Uk3Yb5vls7p0VCX6JPGAyv\n";
        key += "062k0eITND4bsnYSsiUliWODaittHronyQ3OFNIkjEgbmA7+ulh+Bw==\n";
        key += "-----END RSA PRIVATE KEY-----\n";

        outfile << key;
    }

    GCP gcp("test", "test", "test@test.com", filename);
    REQUIRE(!strcmp(gcp.getIAMAddress(), "169.254.169.254"));
    REQUIRE(gcp.getIAMPort() == 80);

    auto dv = gcp.downloadInstanceInfo();
    string resultString = "GET /computeMetadata/v1/instance/machine-type HTTP/1.1\r\nHost: 169.254.169.254\r\nMetadata-Flavor: Google\r\n\r\n";
    REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

    auto p = pair<uint64_t, uint64_t>(numeric_limits<uint64_t>::max(), numeric_limits<uint64_t>::max());
    dv = gcp.getRequest("a/b/c.d", p);
    resultString = "GET /a/b/c.d?X-Goog-Algorithm=GOOG4-RSA-SHA256&X-Goog-Credential=test%40test.com%2F21000101%2Ftest%2Fstorage%2Fgoog4_request&X-Goog-Date=21000101T000000Z&X-Goog-Expires=3600&X-Goog-SignedHeaders=host&x-goog-signature=a1c87c0ad67997db2ee05a5a24a711374bca17edac0ce4d43f435b7e48d9c5c9eafd0a1da4cea1ffbe4d0e2cd6da1433556b97166b302e27d7ba5213739d5e054a6f8610676a13b696f2068aac6af1399f307b83c8a700d4adc6bfba371959fdcbcc8de94ee5d753a8a3d22c006e5176410cee3c1e026b1309440de0ceb21aae3ce7f2699111d5964fa487a7d22a5434d21598b53e254c81e004a98bb9df7bb159e1cd4ec49dae1d1451aad8119e6b433722adb044709fcdcbd394b46552dcd4936e2bd04ea1db9a58f792027e8a60e47553338c92f30baa056738c6e4fac681ebd7d79b3be116bdc3d5dd20823581a27f356d160f8875fda7d704abcb6ebfd6ca8ed986da5ff524da0aefbcdbc6c5be7556f8c551ca873bdb7a619f7816cf575c28a50740efaa2616c75f660dfec184f66afe2a7ea22e26418fc2548104391563994bd28c565ed710ad0fd325dcc7cf281e505bc28660f91cfbb07d2ef81398ef8c15dfb697695dd17c9f561999f866b4127adfa74d37ed3b33a3f499c1ccf3 HTTP/1.1\r\nHost: test.storage.googleapis.com\r\n\r\n";
    REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

    utils::DataVector<uint8_t> putData(10);
    dv = gcp.putRequest("a/b/c.d", string_view(reinterpret_cast<const char*>(putData.data()), putData.size()));
    resultString = "PUT /a/b/c.d?X-Goog-Algorithm=GOOG4-RSA-SHA256&X-Goog-Credential=test%40test.com%2F21000101%2Ftest%2Fstorage%2Fgoog4_request&X-Goog-Date=21000101T000000Z&X-Goog-Expires=3600&X-Goog-SignedHeaders=content-length%3Bdate%3Bhost&x-goog-signature=64a1ebc70a5e5cde6e75ac2a14a0967d36c097de6d464c97e07a0b25e7292bc28c908f407ab4f6c19e21f0868e5774d9c170215a8e72bd327d160e61ec0f4596943924b3c25854826886f626757467839d3bc167d3b8e1b889ae9bf48460eb7e62e2a0e490baeec05fa819704184e18c472dfd8a445806b332f50bfc551dbb49b39998dbe79a55b53287e4bc1d49c98f8263fb399c6eed21b247d1fa7412b886a28b69b7145a1a29c2c648d32df40df6cc2aed8dda64da52e0731fd6449844869eff9614ab2ea198b823f1f68f09c593a9a3be77223ef96f0f5908761a8e6ec9ffa7a382376a1ff18d043e8800ec8953eb61b0e2ca7732e3c4bb83e92743fa8d97b013222370a951d379607e59c7279727e9cd8bd711da985bf123ee4fb68e4ce48865ac1956497e585d5768213db762ec859855a5fdb7083f0a219a535d8a8e20659bcaad6897de1ddf87a73d48c16c7ccbca7e972517417f741aae0fed3bb4c9838d4aec52c06425238fc3c8b2b665d089f50bcedf48762f30c9650e603531 HTTP/1.1\r\nContent-Length: 10\r\nDate: ";
    resultString += gcp.fakeAMZTimestamp;
    resultString += "\r\nHost: test.storage.googleapis.com\r\n\r\n";
    REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

    auto vec = GCPInstance::getInstanceDetails();
    REQUIRE(vec.size() > 0);

    unlink(filename);
    Provider::testEnviornment = false;
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace cloud
} // namespace anyblob

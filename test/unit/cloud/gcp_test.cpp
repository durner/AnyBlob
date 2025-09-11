#include "cloud/gcp.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/gcp_instances.hpp"
#include "cloud/gcp_signer.hpp"
#include "utils/data_vector.hpp"
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::cloud::test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
// Helper to test private methods
class GCPTester {
    public:
    void test() {
        Provider::testEnviornment = true;

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

        auto provider = Provider::makeProvider("gcp://test:test/", false, "test@test.com", key);
        GCP& gcp = *static_cast<GCP*>(provider.get());
        REQUIRE(!gcp.getIAMAddress().compare("169.254.169.254"));
        REQUIRE(gcp.getIAMPort() == 80);

        auto dv = gcp.downloadInstanceInfo();
        string resultString = "GET /computeMetadata/v1/instance/machine-type HTTP/1.1\r\nHost: 169.254.169.254\r\nContent-Length: 0\r\nMetadata-Flavor: Google\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        auto p = pair<uint64_t, uint64_t>(numeric_limits<uint64_t>::max(), numeric_limits<uint64_t>::max());
        dv = gcp.getRequest("a/b/c.d", p);
        resultString = "GET /a/b/c.d?X-Goog-Algorithm=GOOG4-RSA-SHA256&X-Goog-Credential=test%40test.com%2F21000101%2Ftest%2Fstorage%2Fgoog4_request&X-Goog-Date=21000101T000000Z&X-Goog-Expires=3600&X-Goog-SignedHeaders=content-length%3Bhost&x-goog-signature=4d52e029caa27a065af6b04b2aa9518bb2dd51f2d7e748241a86329460bd7374050687b6ba04a5d898ffa45f8e8eb8b71fb2c846b1781fba9975fc67774f51cd0df3a02f8c5912a8cc25e934ced97a61c01545419c292f411140b75ac05c26cb6ff4fba7ba39bcb4e86ebcc711f0f1cf3257e1d0a7a1f1c15f04a2136b29cd3cbd545d2cb13656337406cad17ccbfe3fc406187a3a336fdb83ad9f2012083938524001b244090265ea97b565ffc616fd2070fd98246141bb1382b9ebfed5adba50676e390ac417c60a174ffbf0613a9ade3c7fa173cf0139a9cff15b998137469a6d52907a6730e7c928597ba902559baa286250c27ebebc81f7e9c93dc08fdd66f58e1a967a2da22725b0a73712862b1912d2bad7e642493a4eebed9a1d65d6ec2fd7fa9c14a63f1c91aac1660b51b4ddae74980a402ad159fac2624b148e12342687f656974cdf90e7c009f8f143358e784a48153e185df7c7858d96ddbe7d5db251096fd0afd28ef9fef86f6262e893c652b8a3179b844e3923a52bac8de4 HTTP/1.1\r\nContent-Length: 0\r\nHost: test.storage.googleapis.com\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        utils::DataVector<uint8_t> putData(10);
        dv = gcp.putRequest("a/b/c.d", string_view(reinterpret_cast<const char*>(putData.data()), putData.size()));
        resultString = "PUT /a/b/c.d?X-Goog-Algorithm=GOOG4-RSA-SHA256&X-Goog-Credential=test%40test.com%2F21000101%2Ftest%2Fstorage%2Fgoog4_request&X-Goog-Date=21000101T000000Z&X-Goog-Expires=3600&X-Goog-SignedHeaders=content-length%3Bdate%3Bhost&x-goog-signature=64a1ebc70a5e5cde6e75ac2a14a0967d36c097de6d464c97e07a0b25e7292bc28c908f407ab4f6c19e21f0868e5774d9c170215a8e72bd327d160e61ec0f4596943924b3c25854826886f626757467839d3bc167d3b8e1b889ae9bf48460eb7e62e2a0e490baeec05fa819704184e18c472dfd8a445806b332f50bfc551dbb49b39998dbe79a55b53287e4bc1d49c98f8263fb399c6eed21b247d1fa7412b886a28b69b7145a1a29c2c648d32df40df6cc2aed8dda64da52e0731fd6449844869eff9614ab2ea198b823f1f68f09c593a9a3be77223ef96f0f5908761a8e6ec9ffa7a382376a1ff18d043e8800ec8953eb61b0e2ca7732e3c4bb83e92743fa8d97b013222370a951d379607e59c7279727e9cd8bd711da985bf123ee4fb68e4ce48865ac1956497e585d5768213db762ec859855a5fdb7083f0a219a535d8a8e20659bcaad6897de1ddf87a73d48c16c7ccbca7e972517417f741aae0fed3bb4c9838d4aec52c06425238fc3c8b2b665d089f50bcedf48762f30c9650e603531 HTTP/1.1\r\nContent-Length: 10\r\nDate: ";
        resultString += gcp.fakeAMZTimestamp;
        resultString += "\r\nHost: test.storage.googleapis.com\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        dv = gcp.deleteRequest("a/b/c.d");
        resultString = "DELETE /a/b/c.d?X-Goog-Algorithm=GOOG4-RSA-SHA256&X-Goog-Credential=test%40test.com%2F21000101%2Ftest%2Fstorage%2Fgoog4_request&X-Goog-Date=21000101T000000Z&X-Goog-Expires=3600&X-Goog-SignedHeaders=content-length%3Bhost&x-goog-signature=593ae6ef0a7b54a2934dba440397a223032536b4ffdfbbdcbf467b67b291abe0a2fca2a4e4ee24840d84e0621d5c055cec772b004a6a3c29bdc053d4a51a80a37115db02932ec6def88714131a5d7d5f9594801ff817ddd1ce76d5a1c6f205376bb5d69ee431608d008d153072177fce80b8f41f40306b8d3fc81a7bdc2f7eaa8828876795264a84707aec5d0fec96944c45a9c5a730554d3f6127b902a9b89fe85c4e037f8a0c4878fb240a5994895b8cc578c07e53042cb3ac45a83136b5e3c67ed301009949947776757cab1f64b106fc07f9c63616b7802d1102f7464ca89d0a5fdf523c7172815961c5a842621608c59d6b2099443f8d7a8a38cf3eff897f9ce15cbd564adb29a60c8b90e58633250a55aff3b688d6c21b7ca9ab4f9c6fcba91096636d5b4b79e0fc04713a68a248f063a8eebc3c92b140849ad733b56cb06fd7775d8ff436d8c16ac88a945889c0c997c73210868ecc4150e5faded57fc885c28303b26ce81d782be9fa4e37844feae1b45d84455e19339109834e32a8 HTTP/1.1\r\nContent-Length: 0\r\nHost: test.storage.googleapis.com\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        ignore = GCPInstance::getInstanceDetails();

        Provider::testEnviornment = false;
    }
};
//---------------------------------------------------------------------------
TEST_CASE("gcp") {
    GCPTester tester;
    tester.test();
}
//---------------------------------------------------------------------------
} // namespace anyblob::cloud::test

FROM docker-registry.redmatter.com/redmatter/centos5-legacy-builder:1.3.3

# Download below packages from redmatter repo into tests directory
# - boost_1_54_0-1.54.0-36615.x86_64.rpm (prod)
# - boost_1_54_0-devel-1.54.0-36615.x86_64.rpm (prod)
# - gtest-1.6.0-26537.x86_64.rpm (qa)
# - RM-librml-devel-1.0.0-43187.noarch.rpm (dev)
RUN ( \
    /yum install -y gcc44 gcc44-c++ make automake libicu; \
    /yum install -y boost_1_54_0 boost_1_54_0-devel mysql++-devel libs3-devel cryptopp-devel; \
    /yum qa install -y gtest-1.6.0-26537; \
    /yum dev install -y RM-librml-devel pugixml cpp-netlib fcgi-devel threadpool; \
)

ENV CC=gcc44 CXX=g++44

WORKDIR /code

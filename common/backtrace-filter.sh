#!/bin/bash

sed -i '
# password from rml-database-mysql
s#password = ".*"#password = "XXX"#g;

# coreapi auth_creds
s#auth_creds_ = ".*"#auth_creds_ = "XXX"#g

' "${1:-/dev/null}"

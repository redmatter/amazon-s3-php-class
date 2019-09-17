#!/bin/bash

sed -i '
# password from rml-database-mysql
s#password = "[^"]\+"#password = "XXX"#g;

# coreapi auth_creds
s#auth_creds_ = "[^"]\+"#auth_creds_ = "XXX"#g

# coreapi Baasic Auth header
s#"Basic [^"]\+"#"Basic XXX"#g

' "${1?Backtrace file not given}"

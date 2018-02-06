# Estuary - The Archiving Inlet

It implements an HTTP interface to ArchiveD.

### Example

```
curl -vvv \
    -H "Expect: 100-continue" \
    -H "X-Correlation-ID: $(uuidgen)" \
    -H "Content-Type: audio/wav" \
    -H "Content-MD5: $(openssl md5 -binary < ../collectd/tests/cdrgen/data/with-call-analysis/bLeg_uuid.wav | base64)" \
    -X PUT -T ../collectd/tests/cdrgen/data/with-call-analysis/bLeg_uuid.wav \
    "localhost:8010/recording/1/$(uuidgen)?policy=1999&time=$(date +%s)&legUuid=$(uuidgen)"
```

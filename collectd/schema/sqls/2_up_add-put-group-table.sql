BEGIN TRANSACTION;

CREATE TABLE "PutGroup" (
    "ID"         BLOB NOT NULL COLLATE BINARY PRIMARY KEY UNIQUE,
    "ArchivedIP" INTEGER DEFAULT 0 NOT NULL,
    "RefCount"   INTEGER DEFAULT 0 NOT NULL,
    "Done"       BOOLEAN DEFAULT 0 NOT NULL,
    "Expires"    INTEGER DEFAULT 0
);

CREATE INDEX "idx_PutGroup_Done" ON "PutGroup" ("Done");
CREATE INDEX "idx_PutGroup_Expires" ON "PutGroup" ("Expires");

COMMIT;

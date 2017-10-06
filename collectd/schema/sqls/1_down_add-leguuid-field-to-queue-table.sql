BEGIN TRANSACTION;
    PRAGMA foreign_keys=OFF;

    CREATE TEMPORARY TABLE Queue_Temp (
      ID                  integer PRIMARY KEY AUTOINCREMENT NOT NULL UNIQUE,
      ParentID            integer NOT NULL DEFAULT 0,
      FileType            integer NOT NULL,
      OrgID               integer NOT NULL,
      UserIDs             varchar(255) NOT NULL COLLATE BINARY,
      UUID                blob NOT NULL COLLATE BINARY,
      OriginatorUUID      blob COLLATE BINARY DEFAULT NULL,
      Path                varchar(255) NOT NULL,
      ModifiedPath        varchar(255) DEFAULT NULL,
      State               integer NOT NULL,
      PolicyID            integer NOT NULL DEFAULT 0,
      MD5                 blob NOT NULL,
      AddToCDRMunchCache  integer NOT NULL DEFAULT 0,
      BufferedRecording   integer NOT NULL DEFAULT 0,
      DataFormat          char(20) NOT NULL,
      Retry               integer NOT NULL,
      StartEpoch          integer NOT NULL,
      UpdateTime          datetime NOT NULL
    );

    INSERT INTO Queue_Temp
        SELECT ID, ParentID, FileType, OrgID, UserIDs, UUID, OriginatorUUID, Path, ModifiedPath,
            State, PolicyID, MD5, AddToCDRMunchCache, BufferedRecording, DataFormat, Retry, StartEpoch, UpdateTime
        FROM Queue;
    DROP TABLE Queue;

    CREATE TABLE Queue (
      ID                  integer PRIMARY KEY AUTOINCREMENT NOT NULL UNIQUE,
      ParentID            integer NOT NULL DEFAULT 0,
      FileType            integer NOT NULL,
      OrgID               integer NOT NULL,
      UserIDs             varchar(255) NOT NULL COLLATE BINARY,
      UUID                blob NOT NULL COLLATE BINARY,
      OriginatorUUID      blob COLLATE BINARY DEFAULT NULL,
      Path                varchar(255) NOT NULL,
      ModifiedPath        varchar(255) DEFAULT NULL,
      State               integer NOT NULL,
      PolicyID            integer NOT NULL DEFAULT 0,
      MD5                 blob NOT NULL,
      AddToCDRMunchCache  integer NOT NULL DEFAULT 0,
      BufferedRecording   integer NOT NULL DEFAULT 0,
      DataFormat          char(20) NOT NULL,
      Retry               integer NOT NULL,
      StartEpoch          integer NOT NULL,
      UpdateTime          datetime NOT NULL
    );

    CREATE UNIQUE INDEX idx_FileType_UUID ON Queue (FileType, UUID);
    CREATE INDEX idx_OrgID ON Queue (OrgID);
    CREATE INDEX idx_ParentID ON Queue (ParentID);
    CREATE INDEX idx_Retry ON Queue (Retry);
    CREATE INDEX idx_StartEpoch ON Queue (StartEpoch);
    CREATE INDEX idx_State ON Queue (State);
    CREATE INDEX idx_UpdateTime ON Queue (UpdateTime);

    INSERT INTO Queue SELECT * FROM Queue_Temp;

    DROP TABLE Queue_Temp;
COMMIT;

* Clear empty directories... in cache etc.

Purge TODO
* Identify Orgs and till where we have made progress
** OrgID
** MaxRetentionID
** ResetDate

* Enqueue Retention Records for purge, limit 100s
** Mark them as State=PurgeQueued
** Collect Identifier, EndpointID, and Size (from meta headers via HEAD)
** RetentionPurgeQueue
*** OrgID
*** FileType
*** UUID
*** RealSize
*** StartTime (to work with PurgeLag)
*** State
| EnqueuedPendingApproval | -> | ApprovedYes | -> | PurgeFileBatched | -> | PurgeFileBatchedYes | -> | PurgeUsageBatched | -> | PurgeUsageBatchedYes | -> | PurgeRetentionBatched | -> | PurgeRetentionBatchedYes | -> | Done |
|                         | -> | ApprovedNo  |    |                  | -> | PurgeFileBatchedError |  |                   | -> | PurgeUsageBatchedError |  |                       | -> | PurgeRetentionBatchedError |  |      |

* Batch Deletes
** Batch by EndpointID in 1000 batches; It could involve multiple Orgs
** Verify Metadata from S3 against Retention DB to make sure there is no corruption
** Perform batch delete REST call
** Update Retention records for any other than CDR with PurgeDate=NOW() and State=Purged


* CDRs to be deleted via bucket policy on S3



# Generate the base TSV
# ''''''''''''''''''''''
# On each of the FSS boxes
#
# cd /mnt/store01/recordings
# touch ~/migration_20110323 && \
# find $( pwd ) -type f -mmin +$((24*60)) |
# while read f; do
#   echo $f$'\t'$(du -b $f | awk '{print $1}')$'\t'$(md5sum $f | awk '{print $1}')$'\t'$(stat -c %Y $f);
# done |
# tee -a ~/migration_recordings_20110323.tsv

# Cleanup the data of any anomalies
# '''''''''''''''''''''''''''''''''
# for s in {s01,gmt}fss0{1,2}; do
#   grep -P '^\/mnt\/store01\/recordings\/[0-9]+\/201[01]_[0-9]{2}_[0-9]{2}\/[-0-9a-f]{36}.wav\t' tmp/${s}_migration_recordings_20110323.tsv > tmp/${s}_migration_recordings_20110323_cleaned.tsv;
# done

# Generate SQL
# ''''''''''''
# Run this with the bash scriptlet below
#
# for s in {s01,gmt}fss0{1,2}; do \
#   awk -F'(\t|/)' '{ print $0"\t"$4"\t"$5"\t"$7 }' tmp/${s}_migration_recordings_20110323_cleaned.tsv |
#   awk -v HostName=$s -f ThisFile.awk > tmp/${s}_migration_recordings_20110323.sql&
# done;
# wait

BEGIN {
  c=1;
  sql_insert="INSERT INTO Migration (Path, Size_"HostName", MD5_"HostName", CreateEpoch, Type, OrgID, UUID) VALUES\n";
  sql_dup="\nON DUPLICATE KEY UPDATE Size_"HostName"=VALUES(Size_"HostName"), MD5_"HostName"=VALUES(MD5_"HostName");";
  
  pairs="";
  sep="";
  if(0 == length(OutputFormat))
    OutputFormat = "SQL";
}

{
  gsub(/-/,"",$7);
  $7=substr($7,0,32);
  
  if(OutputFormat == "SQL") {
    if(length(pairs)>0)
  	  sep=",\n";
  	
    pairs = pairs""sep"(\""$1"\","$2",0x"$3","$4",\""$5"\","$6",0x"$7")";
  
    c = ( c + 1 ) % 500;
  
    if(c == 0)
    {
  	  print sql_insert pairs sql_dup;
  	  sep="";
  	  pairs="";
    }
  }
  else if(OutputFormat == "TSV") {
    print $1"\t"$2"\t"$3"\t"$4"\t"$5"\t"$6"\t"$7;
  }
}

END {
  if(length(pairs)>0 && OutputFormat == "SQL")
  	print sql_insert pairs sql_dup;
}

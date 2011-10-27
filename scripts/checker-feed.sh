#!/bin/sh

host=$(uname -n|cut -d. -f1)

if [ -z "$1" ] || [ "$1" -lt 1 ]; then
	n_days=1
else
	n_days="$1"
fi

if [ -z "$2" ]; then
	types="cdr recording"
else
	types="$2"
fi

days_list=$(
php -E '
if ('$n_days' > 1)
	echo "{";
for ($i = 0; $i < '$n_days'; $i++)
	echo ($i? ",":"").strftime("%Y%m%d", time() - $i * 60 * 60 * 24);
if ('$n_days' > 1)
	echo "}";
' < /dev/null
);


for type in $types; do
	case $type in
	cdr)
		# Generate CDR uuids from freeswitch log
		eval ls /var/log/freeswitch.log-$days_list.gz |
		while read f; do
			date=${f##*-}; date=${date%%.gz};
			sudo zgrep 'New Channel sofia/' $f | awk -F'\\[|\\]' '{print $(NF-1)",1,1,'$date','$host'"}';
		done > /tmp/uuids.txt

		# Generate CDR uuids from CollectD log; those which were discarded due to various reasons into the same file as above
		eval ls /var/log/archiving-collectd.log-$days_list.gz |
		while read f; do
			date=${f##*-}; date=${date%%.gz};
			sudo zgrep 'state:discarded done' $f | awk -F '/| state' '{print $7",1,1,'$date','$host'"}';
		done >> /tmp/uuids.txt

		# now remove duplicates ( discarded ones )
		sort /tmp/uuids.txt | uniq -c | awk '{if($1==1) print $NF;}'
		;;
		
	recording)
		# Grab recording UUIDs that were handled by CollectD from the fseventfilemonitor's log
		eval ls /var/log/fseventfilemonitor.log-$days_list.gz |
		while read f; do
			date=${f##*-}; date=${date%%.gz};
			sudo zgrep 'processed by collectd' $f | awk -F'/rmrecordings/|_|.wav' '{print $6","$2",2,'$date','$host'"}'
		done
		;;
		
	sms)
		;;
	esac
done


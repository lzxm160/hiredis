cat $1 |awk -F ' ' '{print $5}' |awk -F ',' '{print $2}'|awk -F ':' '{if($2!=""){print $2}}' > /tmp/iptmp.tmp;
cat $1 |awk -F ' ' '{print $5}' |awk -F ',' '{print $3}'|awk -F ':' '{if($2!="" && $2!="10.21.66.201" && $2!="10.21.0.222" && $2!="10.21.0.210"){print $2}}' >> /tmp/iptmp.tmp;
cat /tmp/iptmp.tmp |sort|uniq -c -i|sort -k 1 -rn|sed -n '1,20p';
cat /dev/null > /tmp/iptmp.tmp;

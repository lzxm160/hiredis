ALL=`cat $1 | wc -l`;
CM=`cat $1 |awk -F ' ' '{print $5}'|awk -F ',' '{print $1}' |awk -F ':' '{if($2=="ChinaMobile"){print $2}}' |wc -l`;
CT=`cat $1 |awk -F ' ' '{print $5}'|awk -F ',' '{print $1}' |awk -F ':' '{if($2=="ChinaTelecom"){print $2}}' |wc -l`;
Other=$[ALL-CM-CT];
echo all:$ALL;
echo chinamobile:$CM;
echo chinatelecom:$CT;
echo other:$Other;


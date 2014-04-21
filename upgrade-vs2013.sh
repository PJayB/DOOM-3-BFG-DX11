#!/bin/bash

for proj in $(find . -name '*VS2012.vcxproj'); do

# get the new filename
	newproj=$(echo $proj | sed -e "s/VS2012/VS2013/g")
	echo Upgrading $proj ... $newproj

# copy the contents
	cp -f $proj $newproj

# upgrade file references
	sed -i -e "s/VS2012/VS2013/g" $newproj

# upgrade the toolset version
	sed -i -e "s/ToolsVersion=\"4.0\"/ToolsVersion=\"12.0\"/g" $newproj

# upgrade the platform toolset version
	sed -i -e "s/<PlatformToolset>v110<\/PlatformToolset>/<PlatformToolset>v120<\/PlatformToolset>/g" $newproj

# upgrade the min vs version
	sed -i -e "s/<MinimumVisualStudioVersion>11.0<\/MinimumVisualStudioVersion>/<MinimumVisualStudioVersion>12.0<\/MinimumVisualStudioVersion>/g" $newproj

done


for proj in $(find . -name '*VS2012.vcxproj.filters'); do

# get the new filename
	newproj=$(echo $proj | sed -e "s/VS2012/VS2013/g")
	echo Upgrading $proj ... $newproj

# copy the contents
	cp -f $proj $newproj

# upgrade file references
	sed -i -e "s/VS2012/VS2013/g" $newproj

done


# upgrade the solution file
for sln in $(find . -name '*VS2012.sln'); do
	newsln=$(echo $sln | sed -e "s/VS2012/VS2013/g")
	echo Upgrading $sln ... $newsln

	cp -f $sln $newsln

	# replace any mention of 2012 with 2013	
	sed -i -e "s/2012/2013/g" $newsln
done





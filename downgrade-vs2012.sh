#!/bin/bash

for proj in $(find . -name '*VS2013.vcxproj'); do

# get the new filename
	newproj=$(echo $proj | sed -e "s/VS2013/VS2012/g")
	echo Downgrading $proj ... $newproj

# copy the contents
	cp -f $proj $newproj

# upgrade file references
	sed -i -e "s/VS2013/VS2012/g" $newproj

# upgrade the toolset version
	sed -i -e "s/ToolsVersion=\"12.0\"/ToolsVersion=\"4.0\"/g" $newproj

# upgrade the platform toolset version
	sed -i -e "s/<PlatformToolset>v120<\/PlatformToolset>/<PlatformToolset>v110<\/PlatformToolset>/g" $newproj

# upgrade the min vs version
	sed -i -e "s/<MinimumVisualStudioVersion>12.0<\/MinimumVisualStudioVersion>/<MinimumVisualStudioVersion>11.0<\/MinimumVisualStudioVersion>/g" $newproj

done


for proj in $(find . -name '*VS2013.vcxproj.filters'); do

# get the new filename
	newproj=$(echo $proj | sed -e "s/VS2013/VS2012/g")
	echo Downgrading $proj ... $newproj

# copy the contents
	cp -f $proj $newproj

# upgrade file references
	sed -i -e "s/VS2013/VS2012/g" $newproj

done


# upgrade the solution file
for sln in $(find . -name '*VS2013.sln'); do
	newsln=$(echo $sln | sed -e "s/VS2013/VS2012/g")
	echo Downgrading $sln ... $newsln

	cp -f $sln $newsln

	# replace any mention of 2012 with 2013	
	sed -i -e "s/2013/2012/g" $newsln
done





# RadioMap

A web app for calculating and displaying radio coverage on a map based on [Signal-Server](https://github.com/lmux/Signal-Server).


## Demo 

[Example](http://[2a02:810a:940:51e4::e0a9]:8080/) using [SRTM3 height data in the region of Leipzig](https://dds.cr.usgs.gov/srtm/version2_1/SRTM3/Eurasia/) and [Ubiquiti .ant files](https://help.ui.com/hc/en-us/articles/204952114-airMAX-Antenna-Data).


## Motivation

Before RadioMap, when calculating and displaying radio coverage on a map, users had to rely in proprietary software or services which limited the functionality to certain antennas or exposed their planned antenna configuration. 
Networks should be built and controlled by the people who use them, with tools that are freely available, without profiting off the necessity to communicate.

## Building the project

- Clone the repo: `git clone https://github.com/lmux/RadioMap/`
- Build Signal-Server
```
cd src/main/resources/Signal-Server/src/
make install
```
For more information how to use Signal-Server, see https://github.com/lmux/Signal-Server
- Compile and run the project with `./mvnw spring-boot:run`
Make sure that the system variable `$JAVA_HOME` used by maven to compile the project points to the Java Runtime Environment used in `pom.xml`.

A `.jar` file can then be built with `java -jar build/libs/RadioMap.jar`.


## Dependencies / Used Leaflet Plugins
The project uses Spring MVC to create a RESTful Web Service. See [example](https://spring.io/guides/gs/rest-service/) and [Documentation for Spring Boot](https://spring.io/projects/spring-boot#overview).
[Leaflet](https://github.com/Leaflet/Leaflet) is used to create the interactive map.
The Leaflet plugin [Leaflet.Toolbar](https://github.com/Leaflet/Leaflet.toolbar) is used to create the toolbar on the map.
[JSZip](https://github.com/Stuk/jszip/) is used to export the markers representing antennas with its properties and coverage from the map as a .kmz file.

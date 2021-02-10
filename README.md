# RadioMap

A web app for displaying radio coverage on a map, using [Signal-Server](https://github.com/lmux/Signal-Server) to calculate the coverage.


## Demo 

See http://[2a02:810a:940:51e4::e0a9]:8080/ . Using [SRTM3 height data in the region of Leipzig](https://dds.cr.usgs.gov/srtm/version2_1/SRTM3/Eurasia/) and [Ubiquiti .ant files](https://help.ui.com/hc/en-us/articles/204952114-airMAX-Antenna-Data).

See http://[2a02:810a:940:51e4::e0a9]:8080/swagger-ui.html for API documentation.

## Motivation

Before RadioMap, when calculating and displaying radio coverage on a map, users had to rely in proprietary software or services which limited the functionality to certain antennas or exposed their planned antenna configuration. 
Networks should be built and controlled by the people who use them, with tools that are freely available, without profiting off the necessity to communicate.

## Build the project

- Clone this repo with submodule Signal-Server: `git clone --recursive https://github.com/lmux/RadioMap/`

- Build Signal-Server: 
```
cd src/main/resources/Signal-Server/src/
make install
```
[More information how to use Signal-Server](https://github.com/lmux/Signal-Server)
- Compile and run the project: `./mvnw spring-boot:run`

Make sure that the system variable `$JAVA_HOME` used by maven to compile the project points to the same version of the Java Runtime Environment used in `pom.xml` (currently 1.8).

A `.jar` file can then be built with `./mvnw clean package` and run with `java -jar target/RadioMap-0.0.1-SNAPSHOT.jar`.

## Add height and antenna data

The web app is currently set up to use SRTM3 height data from `src/main/resources/Signal-Server/data/SRTM3`.
SRTM3 height data can be downloaded from [USGS](https://dds.cr.usgs.gov/srtm/version2_1/SRTM3/Eurasia/) and converted into the format used by Signal-Server with `./Signal-Server/utils/sdf/srtm2sdf`.
The bash script `get_srtm3_sdf.sh` automates these steps.

After starting the web app, the directory `src/main/resources/Signal-Server/antenna/` is scanned for `.el` and `.az` radiation pattern files, which are then added as optional antennas on the website to create the coverage from.

These files can be created by getting the .ant file, usually found on the manufacturers website of the (directional) antenna. Convert the .ant file with `./src/main/resources/Signal-Server/utils/antenna/ant2azel.py` (python3 needed)
and place the resulting `.el` and `.az` files in the above mentioned directory.

## Dependencies and Plugins

The dependencies used on the back end are listed in the `pom.xml`:

- Spring MVC (`spring-boot-starter-web`) is used to create a RESTful Web Service. See [example](https://spring.io/guides/gs/rest-service/) and [Documentation for Spring Boot](https://spring.io/projects/spring-boot#overview).

- Thymeleaf (`spring-boot-starter-thymeleaf`) is used to create the website from the template.

- `spring-boot-starter-test` is used for the test class for creating a coverage.

- `springdoc-openapi-ui` is used for documenting the API based on the OpenAPI specification.

- The plugin `maven-compiler-plugin` is used for compiling the project with maven.

The dependencies used on the front end are listed under `src/main/resources/static/`:

- [Leaflet](https://github.com/Leaflet/Leaflet) is used to create the interactive map.

- The Leaflet plugin [Leaflet.Toolbar](https://github.com/Leaflet/Leaflet.toolbar) is used to create the toolbar on the map.

- [JSZip](https://github.com/Stuk/jszip/) is used to export the markers representing antennas with its properties and coverage from the map as a .kmz file.

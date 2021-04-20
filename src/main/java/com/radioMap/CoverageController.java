package com.radioMap;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.media.Content;
import io.swagger.v3.oas.annotations.media.Schema;
import io.swagger.v3.oas.annotations.responses.ApiResponse;
import io.swagger.v3.oas.annotations.responses.ApiResponses;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.io.*;
import java.nio.file.Files;

/**
 * Class used for requests to the coverages directory.
 * Used for generating and returning the coverage of an antenna specified in the request.
 *
 * @author Michael Lux
 * @author www.lmux.de
 * @version 1.0
 * @since 1.0
 */
@RestController
public class CoverageController {
    /**
     * Generates the requested coverage of an antenna and returns its path,
     * the status of the creation and the used parameters.
     *
     * @param {double} lat The latitude of the transmitting antenna in decimal degrees (EPSG:4326 WGS 84)
     * @param {double} lon The longitude of the transmitting antenna in decimal degrees (EPSG:4326 WGS 84)
     * @param {double} txh The height of the transmitting antenna in meters
     * @param {double} erp The total effective radiated power including transmitter and receiver gain in watts (dBd)
     * @param {double} rot The transmitting antenna radiation pattern rotation (0.0 - 359.0 degrees, default 0)
     * @param {String} ant The name of the transmitting antenna, used for selecting radiation pattern (default "omni")
     * @param {double} freq The frequency of the transmitting antenna in megahertz (20MHz to 100GHz, LOS after 20GHz, default "2400")
     * @param {String} pm The propagation model used for generating the coverage (default "ITM")
     * @param {double} dt The downtilt of the transmitting antenna (-10.0 - 90.0 degrees, default 0)
     * @param {double} dtdir The downtilt direction of the transmitting antenna ( 0.0 - 359.0 degrees, default 0)
     * @param {String} pol The polarisation of the transmitting antenna (horizontal/vertical, default vertical)
     * @param {double} rxh The height of the receiving antenna in meters (default 0.1)
     * @return {String} The name of the view, which gets returned.
     */
    @ResponseStatus(HttpStatus.CREATED)
    //takes json and xml (Formdata)
    @Operation(summary = "Generate a coverage")
    @PostMapping("/coverages")
    public Coverage coverage(@RequestParam(value = "lat") double lat,
                             @RequestParam(value = "lon") double lon,
                             @RequestParam(value = "txh") double txh,
                             @RequestParam(value = "erp") double erp,
                             @RequestParam(value = "rot", defaultValue = "0") double rot,
                             @RequestParam(value = "ant", defaultValue = "omni") String ant,
                             @RequestParam(value = "freq", defaultValue = "2400") double freq,
                             @RequestParam(value = "pm", defaultValue = "ITM") String pm,
                             @RequestParam(value = "dt", defaultValue = "0") double dt,
                             @RequestParam(value = "dtdir", defaultValue = "0") double dtdir,
                             @RequestParam(value = "pol", defaultValue = "vertical") String pol,
                             @RequestParam(value = "rxh", defaultValue = "0.1") double rxh
    ) throws IOException, InterruptedException {
        //jackson returns json data with all variables of coverage having getter methods
        //useful for knowing if correct values where used
        return new Coverage(lat, lon, txh, erp, rot, ant, freq, pm, dt, dtdir, pol, rxh).generateCoverage();
    }

    //uuid of coverage necessary in order to get image
    /**
     * Returns the coverage specified by its id, which is located in the coverages directory.
     *
     * @see https://www.baeldung.com/spring-rest-openapi-documentation for used api response annotations
     * @param {String} id The identifier of the requested coverage
     * @return {ResponseEntity<byte[]>} The response including the coverage inside the body as a .png blob
     */
    //https://www.baeldung.com/spring-rest-openapi-documentation
    @Operation(summary = "Get a coverage by its id")
    @ApiResponses(value = {
            @ApiResponse(responseCode = "200", description = "Found the coverage",
                    content = { @Content(mediaType = "image/png") }),
            @ApiResponse(responseCode = "400", description = "Invalid id supplied",
                    content = @Content),
            @ApiResponse(responseCode = "404", description = "Coverage not found",
                    content = @Content) })
    @GetMapping(value = "/coverages/{id:.+}")
    public ResponseEntity<byte[]> getImage(@PathVariable("id") String id) throws IOException {
        File img = new File("src/main/resources/coverages/".concat(id));
        byte[] image = Files.readAllBytes(img.toPath());
        return ResponseEntity.ok().contentType(MediaType.IMAGE_PNG).body(image);
    }
}



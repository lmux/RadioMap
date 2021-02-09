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

@RestController
public class CoverageController {


//    Seite 155 SpringimEinsatz
    @ResponseStatus(HttpStatus.CREATED)
//    takes json and xml(Formdata)
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
//        return new Coverage(lat, lon, freq, txh, rxh, erp, pm).generateCoverage();
        return new Coverage(lat, lon, txh, erp, rot, ant, freq, pm, dt, dtdir, pol, rxh).generateCoverage();
    }

    //    uuid of coverage necessary in order to get image

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



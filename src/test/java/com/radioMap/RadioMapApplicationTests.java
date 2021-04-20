package com.radioMap;

import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;

import java.io.IOException;

import static org.junit.jupiter.api.Assertions.assertEquals;

@SpringBootTest
class RadioMapApplicationTests {

    @Test
    void coverageCreation() throws IOException, InterruptedException {
        double lat = 51.34, lon = 12.4, txh = 5.0, erp= 25, rot = 0.0, freq = 2400, dt= 0.0, dtdir = 0.0, rxh = 0.1;
        String ant = "omni", pm = "ITM", pol = "horizontal";

        Coverage testCoverage  =  new Coverage(lat, lon, txh, erp, rot, ant, freq, pm, dt, dtdir, pol, rxh);
        testCoverage.generateCoverage();

        assertEquals(testCoverage.getStatus(),"coverage created");
    }

}

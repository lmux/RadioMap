package com.radioMap;

import java.io.*;
import java.util.*;

import static java.util.concurrent.TimeUnit.SECONDS;

/**
 * Class used for creating antenna coverages, holding all necessary antenna and coverage parameters.
 *
 * @author Michael Lux
 * @author www.lmux.de
 * @version 1.0
 * @since 1.0
 */
public class Coverage {
    private final static ArrayList<String> radiationPatterns = createRadiationPatternOptions("src/main/resources/Signal-Server/antenna");
    //fixed parameters chosen on backend
    //Signal-Server option 1: ITM, 2: LOS, 3: Hata, 4: ECC33, 5: SUI, 6: COST-Hata, 7: FSPL, 8: ITWOM, 9: Ericsson, 10: Plane earth, 11: Egli VHF/UHF, 12: Soil
    private final static List<String> propagationModels = Arrays.asList("ITM", "LOS", "Hata", "ECC33", "SUI", "COST-Hata", "FSPL", "ITWOM", "Ericsson", "Plane earth", "Egli VHF/UHF", "Soil");
    //TODO: using best available height data in region
    private final static String heightData = "-sdf";
    private final static String heightDataDirSRTM3 = "src/main/resources/Signal-Server/data/SRTM3";
    private final static String heightDataDirLIDAR = "src/main/resources/Signal-Server/data/LIDAR";
    private final static String antennaPatternDataDir = "src/main/resources/Signal-Server/antenna/";
    private final static String coverageFileDir = "src/main/resources/coverages/";
    private final static int maxGenerateCoverageTimeSeconds = 10;
    //TODO: give client option to specify threshold of received signal power/field strength
    private final double rxThreshold = -90;
    //TODO: give client option to choose radius
    private final double radius = 5;    //kilometers with argument -m (metric)
    private final double resolution = 3500;

    //chosen on client-side, only expose these with getters for response
    private final double lat;
    private final double lon;
    private final double txh;
    private final double erp;
    private final double rot;
    private final String ant;
    private final double freq;
    private final String pm;
    private final double dt;
    private final double dtdir;
    private final String pol;
    private final double rxh;

    //results of generating coverage, also expose with getters for response
    private String status;
    private String[] bounds;
    //create id that is unique, hence no collisions, no information gained from name
    //name compatible with file system, other users cant guess files openly available
    private final String covID;
    private String link;

    public Coverage(double lat, double lon, double txh, double erp, double rot, String ant, double freq, String pm, double dt, double dtdir, String pol, double rxh) {
        this.covID = UUID.randomUUID().toString();
        this.lat = lat;
        this.lon = lon;
        this.txh = txh;
        this.erp = erp;
        this.rot = rot;
        this.ant = ant;
        this.freq = freq;
        this.pm = pm;
        this.dt = dt;
        this.dtdir = dtdir;
        this.pol = pol;
        this.rxh = rxh;
        this.status = "not created";
        this.link = "";
    }

    /**
     * Creates the coverage as .ppm and .png files in the coverageFileDir by starting a process of Signal-Server, writing outputs to coverageCreationLog.txt
     *
     * @return {Coverage} The coverage object including antenna and coverage creation attributes, status of creation and link to resource.
     */
    public Coverage generateCoverage() throws IOException, InterruptedException {
        //sanitizing parameters
        if (this.lat < -70.0 || this.lat > 70.0 || this.lon < -180.0 || this.lon > 180.0) {
            this.status = "Coordinates out of bounds";
            return this;
        }
        if (this.txh < 0 || this.txh > 10000) {
            this.status = "Transmitter height out of bounds";
            return this;
        }
        //TODO: find fitting max value for erp
        if (this.erp < 0 || this.erp > 10000) {
            this.status = "Effective Radiated Power out of bounds";
            return this;
        }
        if (this.rot < 0.0 || this.rot >= 360.0) {
            this.status = "Heading out of bounds";
            return this;
        }
        if (!radiationPatterns.contains(this.ant)) {
            this.status = "Invalid Radiation Pattern";
            return this;
        }
        if (this.freq < 20 || this.freq > 100000) {
            this.status = "Frequency out of bounds";
            return this;
        }
        if (!propagationModels.contains(pm)) {
            this.status = "Invalid propagation model selection";
            return this;
        }
        //TODO: check if 359.x is allowed
        if (this.dt < -10.0 || this.dt >= 360.0) {
            this.status = "Downtilt out of bounds";
            return this;
        }
        if (this.dtdir < 0.0 || this.dtdir >= 360.0) {
            this.status = "Downtilt direction out of bounds";
            return this;
        }
        if (!(this.pol.equals("horizontal") || this.pol.equals("vertical"))) {
            this.status = "Invalid polarisation selection";
            return this;
        }
        if (this.rxh < 0 || this.rxh > 10000) {
            this.status = "Receiver height out of bounds";
            return this;
        }
        String coverageFileArg = this.coverageFileDir.concat(this.covID);

        String hpArg = "";
        if (this.pol.equals("horizontal")) hpArg = "-hp";

        String antArg = "";
        //check if default omni was chosen, else add argument
        if (this.ant != radiationPatterns.get(0)) antArg = antennaPatternDataDir.concat(this.ant);


        //argument option and value have to be passed separately, first entry is command
        String[] sigServerArgs = {"./src/main/resources/Signal-Server/signalserver",
                this.heightData, this.heightDataDirSRTM3,
                "-lat", Double.toString(this.lat),
                "-lon", Double.toString(this.lon),
                "-txh", Double.toString(this.txh),
                "-erp", Double.toString(this.erp),
                "-rot", Double.toString(this.rot),
                "-ant", antArg,
                "-f", Double.toString(this.freq),
                "-pm", Integer.toString(propagationModels.indexOf(pm) + 1),//program starts counting at 1
                "-dt", Double.toString(this.dt),
                "-dtdir", Double.toString(this.dtdir),
                "-rxh", Double.toString(this.rxh),
                "-dbm", //TODO: give client option to choose between signal power or field strength (-dbm: signal power)
                "-rt", Double.toString(this.rxThreshold),
                "-m",
                "-R", Double.toString(this.radius),
                "-o", coverageFileArg,
                hpArg};

        ProcessBuilder pb = new ProcessBuilder(sigServerArgs);
//        ProcessBuilder pb = new ProcessBuilder("./src/main/resources/Signal-Server/signalserver", latArg , lonArg, freqArg, txhArg, coverageFileArg);

        //write errors to logfile
        File log = new File("coverageCreationLog.txt");
        pb.redirectErrorStream(true);
//        pb.redirectOutput(ProcessBuilder.Redirect.appendTo(log));
        Process process = pb.start();

//        read output for information about bounds
//        https://stackoverflow.com/questions/8149828/read-the-output-from-java-exec#8150065
        StringBuilder processOutput = new StringBuilder();
        try (BufferedReader processOutputReader = new BufferedReader(
                new InputStreamReader(process.getInputStream()));) {
            String readLine;
            while ((readLine = processOutputReader.readLine()) != null) {
                processOutput.append(readLine + System.lineSeparator());
            }
        }
        //JRE 11 and higher
//        String boundsOutput = processOutput.toString().trim();
        //for downwards compatibility (right trim)
        String boundsOutput = processOutput.toString().replaceAll("\\s+$", "");

//      blocks thread until process is finished
        if (process.waitFor(this.maxGenerateCoverageTimeSeconds, SECONDS)) {
            if (convertToPNG(coverageFileArg) == 0) {
//            normal termination
                this.status = "coverage created";
                this.link = "/coverages/".concat(this.covID).concat(".png");
                this.bounds = boundsOutput.substring(1).split("\\|");
            }
        } else {
//            this.status = process.getErrorStream().toString();
            this.status = "failed to create coverage";
        }
        return this;
    }

    /**
     * Converts the given .ppm file to .png and saves it in same directory as the original file.
     *
     * @param {String} file The .ppm file including its path.
     * @return {int} The exit value of the process represented by this Process object. By convention, the value 0 indicates normal termination.
     */
    public int convertToPNG(String file) throws IOException, InterruptedException {
//        Load Image from https://docs.oracle.com/javase/tutorial/2d/images/loadimage.html
/*        BufferedImage img = null;
        System.out.println(file);
        try {
            img = ImageIO.read(new File(file));
            File outputfile = new File(file.concat(".png");
            ImageIO.write(img, "png", outputfile);
        } catch (IOException e) {
        }*/
        //TODO:make conversion in Java http://im4java.sourceforge.net/ or C++/c
        //best imagemagick api in c, and only write ppm file im memory before writing png
        ProcessBuilder pb = new ProcessBuilder("convert", file.concat(".ppm"), "-transparent", "white", "-channel", "Alpha", file.concat(".png"));
        Process process = pb.start();
        return process.waitFor();
    }

    /**
     * Reads and returns the names of the antennas with .az and .el radiation pattern files in the given directory, adding omnidirectional antenna as first entry.
     *
     * @param {String} radiationPatternFilePath The file path the radiation patterns are located in.
     * @return {ArrayList<String>} The list of antennas with .az and .el files in the given directory.
     */
    private static ArrayList<String> createRadiationPatternOptions(String radiationPatternFilePath) {
        ArrayList<String> azFiles = readRadiationPatternFileNames(radiationPatternFilePath);
        //acts as default option for no radiation pattern
        azFiles.add(0, "omni");
        return azFiles;
    }

    /**
     * Reads and returns the names of the antennas with .az and .el radiation pattern files in given directory.
     *
     * @param {String} radiationPatternFilePath The file path the radiation patterns are located in.
     * @return {ArrayList<String>} The list of antennas with .az and .el files in the given directory.
     */
    private static ArrayList<String> readRadiationPatternFileNames(String radiationPatternFilePath) {
        File folder = new File(radiationPatternFilePath);
        ArrayList<String> azFiles = new ArrayList<>();
        ArrayList<String> elFiles = new ArrayList<>();
        File[] listOfFiles = folder.listFiles();
        assert listOfFiles != null;
        for (File listOfFile : listOfFiles) {
            String fileName = listOfFile.getName();
            if (fileName.endsWith(".az")) {
                //cut off file extension
                azFiles.add(fileName.substring(0, fileName.lastIndexOf('.')));
            } else if (fileName.endsWith(".el")) {
                elFiles.add(fileName.substring(0, fileName.lastIndexOf('.')));
            }
        }
        //only keep files that exist with .az and .el extension
        for (int i = 0; i < azFiles.size(); i++)
            if (!elFiles.contains(azFiles.get(i))) {
                azFiles.remove(i);
            }
        System.out.println("Detected Antenna Radiation Pattern Files in src/main/resources/Signal-Server/antenna: " + azFiles);
        return azFiles;
    }

    public static ArrayList<String> getRadiationPatterns() {
        return radiationPatterns;
    }

    public String getPm() {
        return pm;
    }

    public String getStatus() {
        return status;
    }

    public String getLink() {
        return link;
    }

    public double getLat() {
        return lat;
    }

    public double getLon() {
        return lon;
    }

    public double getTxh() {
        return txh;
    }

    public double getErp() {
        return erp;
    }

    public double getRot() {
        return rot;
    }

    public String getAnt() {
        return ant;
    }

    public double getFreq() {
        return freq;
    }

    public double getDt() {
        return dt;
    }

    public double getDtdir() {
        return dtdir;
    }

    public String getPol() {
        return pol;
    }

    public double getRxh() {
        return rxh;
    }

    public String[] getBounds() {
        return bounds;
    }

    public String getCovID() {
        return covID;
    }

    public static List<String> getPropagationModels() {
        return propagationModels;
    }
}

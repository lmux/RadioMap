package com.radioMap;

import com.nar.NativeApp;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;

import java.io.File;
import java.util.ArrayList;


@SpringBootApplication
public class RadioMapApplication {
    public static void main(String[] args) {
        SpringApplication.run(RadioMapApplication.class, args);
        NativeApp app = new NativeApp();
//        System.out.println( app.sayHello(0, new String[]{"Test"}) );
        System.out.println( app.sayHello(new String[]{"signalserver"}) );
    }

}

package com.radioMap;

import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;

@Controller
public class HomeController {

    @GetMapping("/")
    public String home(Model model) {
        model.addAttribute("radiationPatterns", Coverage.getRadiationPatterns());
        model.addAttribute("propagationModels", Coverage.getPropagationModels());
        return "home";
    }
}

package com.radioMap;

import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;


/**
 * Class used for requests to the home directory.
 * Used for generating and returning the website of the web application.
 *
 * @author Michael Lux
 * @author www.lmux.de
 * @version 1.0
 * @since 1.0
 */
@Controller
public class HomeController {

    /**
     * Adds the lists of radio propagation models and antenna radiation pattern to the model,
     * which are used in the template referenced by the returned home view.
     *
     * @param {Model} The model of the home view.
     * @return {String} The name of the view, which gets returned.
     */
    @GetMapping("/")
    public String home(Model model) {
        model.addAttribute("radiationPatterns", Coverage.getRadiationPatterns());
        model.addAttribute("propagationModels", Coverage.getPropagationModels());
        return "home";
    }
}

import {html, LitElement} from "lit"
import {customElement, state} from "lit/decorators.js"
import '../assets/mui.js'
import {_renderOptions, _uintInput} from "../utils/libs.js"
import {elrsState, saveOptionsAndConfig} from "../utils/state.js"
import {postWithFeedback} from "../utils/feedback.js"

@customElement('rx-options-panel')
class RxOptionsPanel extends LitElement {
    @state() accessor domain
    @state() accessor enableModelMatch
    @state() accessor lockOnFirst
    @state() accessor modelId
    @state() accessor forceTlmOff
    @state() accessor customFreqEnabled
    @state() accessor customFreqStart
    @state() accessor customFreqStop
    @state() accessor customFreqCount

    createRenderRoot() {
        this.domain = elrsState.options.domain
        this.lockOnFirst = elrsState.options['lock-on-first-connection']
        this.enableModelMatch = elrsState.config.modelid!==undefined && elrsState.config.modelid !== 255
        this.modelId = elrsState.config.modelid===undefined ? 0 : elrsState.config.modelid
        this.forceTlmOff = elrsState.config['force-tlm']
        // Custom frequency settings
        this.customFreqEnabled = elrsState.options['custom-freq-enabled'] || false
        this.customFreqStart = elrsState.options['custom-freq-start'] || 900000000
        this.customFreqStop = elrsState.options['custom-freq-stop'] || 930000000
        this.customFreqCount = elrsState.options['custom-freq-count'] || 40
        this.save = this.save.bind(this)
        return this
    }

    render() {
        return html`
            <div class="mui-panel mui--text-title">Runtime Options</div>
            <div class="mui-panel">
                <p><b>Override</b> options provided when the firmware was flashed. These changes will
                    persist across reboots, but <b>will be reset</b> when the firmware is reflashed.</p>
                <form id='upload_options' method='POST' action="/options">
                    <!-- FEATURE:HAS_SUBGHZ -->
                    <div class="mui-select">
                        <select id="domain" @change="${(e) => this.domain = parseInt(e.target.value)}">
                            ${_renderOptions(['FCC915','CUST900','EU868','IN866','AU433','EU433','US433','US433-Wide'], this.domain)}
                        </select>
                        <label for="domain">Regulatory domain</label>
                    </div>
                    <!-- /FEATURE:HAS_SUBGHZ -->
                    <h2>Lock on first connection</h2>
                    RF Mode Locking - Default mode is for the RX to cycle through the available RF modes with 5s pauses
                    going from highest to lowest mode and finding which mode the TX is transmitting. This allows the RX to
                    cycle, but once a connection has been established, the Rx will no longer cycle through the RF modes
                    (until it receives a power reset).
                    <br/>
                    <div class="mui-checkbox">
                        <input id="lock" type='checkbox'
                               ?checked="${this.lockOnFirst}"
                               @change="${(e) => {this.lockOnFirst = e.target.checked}}"/>
                        <label for="lock">Lock on first connection</label>
                    </div>
                    <h2>Model Match</h2>
                    Model Match is used to prevent accidentally selecting the wrong model in the handset and flying with an
                    unexpected handset or ELRS configuration. When enabled, Model Match restricts this receiver to only
                    connect fully with the specific Receiver ID below. Set the transmitter's Receiver ID in EdgeTX's model
                    settings, and enable Model Match in the ExpressLRS lua.
                    <br/>
                    <div class="mui-checkbox">
                        <input id="modelMatch" type='checkbox'
                               ?checked="${this.enableModelMatch}"
                               @change="${(e) => {this.enableModelMatch = e.target.checked}}"/>
                        <label for="modelMatch">Enable Model Match</label>
                    </div>
                    ${this.enableModelMatch ? html`
                    <div class="mui-textfield">
                        <input id="modelId" min="0" max="63" type='number' required
                               @change="${(e) => this.modelId = parseInt(e.target.value)}"
                               .value="${this.modelId}"
                               @keypress="${_uintInput}"/>
                        <label for="modelId">Receiver ID (0 - 63)</label>
                    </div>
                    ` : ''}
                    <h2>Force telemetry off</h2>
                    When running multiple receivers simultaneously from the same TX (to increase the number of PWM servo outputs), there can be at most one receiver with telemetry enabled.
                    <br>Enable this option to ignore the "Telem Ratio" setting on the TX and never send telemetry from this receiver.
                    <br/>
                    <div class="mui-checkbox">
                        <input id='force-tlm' name='force-tlm' type='checkbox'
                               ?checked="${this.forceTlmOff}"
                               @change="${(e) => this.forceTlmOff = e.target.checked}"
                        />
                        <label for="force-tlm">Force telemetry OFF on this receiver</label>
                    </div>

                    <!-- FEATURE:HAS_SUBGHZ -->
                    <h2>Custom Frequency (Advanced)</h2>
                    Override the regulatory domain with custom frequency settings. <b>Both TX and RX must use matching frequencies.</b>
                    Use this to operate on non-standard frequencies for specialized applications.
                    <br/>
                    <div class="mui-checkbox">
                        <input id='custom-freq-enabled' type='checkbox'
                               ?checked="${this.customFreqEnabled}"
                               @change="${(e) => this.customFreqEnabled = e.target.checked}"
                        />
                        <label for="custom-freq-enabled">Enable Custom Frequency</label>
                    </div>
                    ${this.customFreqEnabled ? html`
                    <div class="mui-textfield">
                        <input id="custom-freq-start" type='number' required
                               min="400000000" max="2500000000" step="100000"
                               @change="${(e) => this.customFreqStart = parseInt(e.target.value)}"
                               .value="${this.customFreqStart}"
                               @keypress="${_uintInput}"/>
                        <label for="custom-freq-start">Start Frequency (Hz) - e.g. 900000000 for 900 MHz</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="custom-freq-stop" type='number' required
                               min="400000000" max="2500000000" step="100000"
                               @change="${(e) => this.customFreqStop = parseInt(e.target.value)}"
                               .value="${this.customFreqStop}"
                               @keypress="${_uintInput}"/>
                        <label for="custom-freq-stop">Stop Frequency (Hz) - e.g. 930000000 for 930 MHz</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="custom-freq-count" type='number' required
                               min="4" max="80"
                               @change="${(e) => this.customFreqCount = parseInt(e.target.value)}"
                               .value="${this.customFreqCount}"
                               @keypress="${_uintInput}"/>
                        <label for="custom-freq-count">Channel Count (4-80)</label>
                    </div>
                    <p><i>Common presets: 900 ISM (900-930MHz, 40ch), 868 EU (863-870MHz, 13ch), 915 US (902-928MHz, 40ch)</i></p>
                    ` : ''}
                    <!-- /FEATURE:HAS_SUBGHZ -->

                    <button class="mui-btn mui-btn--primary"
                            ?disabled="${!this.checkChanged()}"
                            @click="${this.save}"
                    >
                        Save
                    </button>
                    ${elrsState.options.customised ? html`
                        <button class="mui-btn mui-btn--small mui-btn--danger mui--pull-right"
                                @click="${postWithFeedback('Reset Runtime Options', 'An error occurred resetting runtime options', '/reset?options', null)}"
                        >
                            Reset to defaults
                        </button>
                    ` : ''}
                </form>
            </div>
        `
    }

    save(e) {
        e.preventDefault()
        const changes = {
            options: {
                // FEATURE: HAS_SUBGHZ
                'domain': this.domain,
                'custom-freq-enabled': this.customFreqEnabled,
                'custom-freq-start': this.customFreqStart,
                'custom-freq-stop': this.customFreqStop,
                'custom-freq-count': this.customFreqCount,
                // /FEATURE: HAS_SUBGHZ
                'lock-on-first-connection': this.lockOnFirst,
            },
            config: {
                'modelid': this.enableModelMatch ? this.modelId : 255,
                'force-tlm': this.forceTlmOff
            }
        }
        saveOptionsAndConfig(changes, () => {
            this.modelId = changes.config.modelid
            return this.requestUpdate()
        })
    }

    checkChanged() {
        let changed = false
        // FEATURE: HAS_SUBGHZ
        changed |= this.domain !== elrsState.options['domain']
        changed |= this.customFreqEnabled !== (elrsState.options['custom-freq-enabled'] || false)
        changed |= this.customFreqStart !== (elrsState.options['custom-freq-start'] || 900000000)
        changed |= this.customFreqStop !== (elrsState.options['custom-freq-stop'] || 930000000)
        changed |= this.customFreqCount !== (elrsState.options['custom-freq-count'] || 40)
        // /FEATURE: HAS_SUBGHZ
        changed |= this.lockOnFirst !== elrsState.options['lock-on-first-connection']
        changed |= this.enableModelMatch && this.modelId !== elrsState.config['modelid']
        changed |= !this.enableModelMatch && this.modelId !== 255
        changed |= this.forceTlmOff !== elrsState.config['force-tlm']
        return !!changed
    }
}

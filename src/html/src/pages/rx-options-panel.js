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
    @state() accessor runtimeFreqEnabled
    @state() accessor runtimeFreqPreset
    @state() accessor runtimeFreqStart
    @state() accessor runtimeFreqStop
    @state() accessor runtimeFreqCount
    @state() accessor runtimeFreqLabel
    @state() accessor runtimeHighFreqEnabled
    @state() accessor runtimeHighFreqPreset
    @state() accessor runtimeHighFreqStart
    @state() accessor runtimeHighFreqStop
    @state() accessor runtimeHighFreqCount
    @state() accessor runtimeHighFreqLabel

    createRenderRoot() {
        this.domain = elrsState.options.domain
        this.lockOnFirst = elrsState.options['lock-on-first-connection']
        this.enableModelMatch = elrsState.config.modelid!==undefined && elrsState.config.modelid !== 255
        this.modelId = elrsState.config.modelid===undefined ? 0 : elrsState.config.modelid
        this.forceTlmOff = elrsState.config['force-tlm']
        this.runtimeFreqEnabled = elrsState.options['runtime-freq-enabled'] || false
        this.runtimeFreqPreset = elrsState.options['runtime-freq-preset'] || 0
        this.runtimeFreqStart = elrsState.options['runtime-freq-start'] || 903500000
        this.runtimeFreqStop = elrsState.options['runtime-freq-stop'] || 926900000
        this.runtimeFreqCount = elrsState.options['runtime-freq-count'] || 40
        this.runtimeFreqLabel = elrsState.options['runtime-freq-label'] || ''
        this.runtimeHighFreqEnabled = elrsState.options['runtime-high-freq-enabled'] || false
        this.runtimeHighFreqPreset = elrsState.options['runtime-high-freq-preset'] || 0
        this.runtimeHighFreqStart = elrsState.options['runtime-high-freq-start'] || 2400400000
        this.runtimeHighFreqStop = elrsState.options['runtime-high-freq-stop'] || 2479400000
        this.runtimeHighFreqCount = elrsState.options['runtime-high-freq-count'] || 80
        this.runtimeHighFreqLabel = elrsState.options['runtime-high-freq-label'] || ''
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
                    <h2>Runtime Frequency Override</h2>
                    This mirrors the active TX override as a fallback/editor surface. For normal use, apply from the TX Lua so the RX is updated and both sides rebind together. X-Band/crossband uses the current sub-GHz plus high-band pair together.
                    <br/>
                    <div class="mui-checkbox">
                        <input id='runtime-freq-enabled' type='checkbox'
                               ?checked="${this.runtimeFreqEnabled}"
                               @change="${(e) => this.runtimeFreqEnabled = e.target.checked}"
                        />
                        <label for="runtime-freq-enabled">Enable runtime sub-GHz override</label>
                    </div>
                    ${this.runtimeFreqEnabled ? html`
                    <div class="mui-textfield">
                        <input id="runtime-freq-preset" type='number' min="0" max="5"
                               @change="${(e) => this.runtimeFreqPreset = parseInt(e.target.value)}"
                               .value="${this.runtimeFreqPreset}"
                               @keypress="${_uintInput}"/>
                        <label for="runtime-freq-preset">Preset Slot</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="runtime-freq-start" type='number' min="700000000" max="960000000"
                               @change="${(e) => this.runtimeFreqStart = parseInt(e.target.value)}"
                               .value="${this.runtimeFreqStart}"
                               @keypress="${_uintInput}"/>
                        <label for="runtime-freq-start">Start Frequency (Hz)</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="runtime-freq-stop" type='number' min="700000000" max="960000000"
                               @change="${(e) => this.runtimeFreqStop = parseInt(e.target.value)}"
                               .value="${this.runtimeFreqStop}"
                               @keypress="${_uintInput}"/>
                        <label for="runtime-freq-stop">Stop Frequency (Hz)</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="runtime-freq-count" type='number' min="4" max="80"
                               @change="${(e) => this.runtimeFreqCount = parseInt(e.target.value)}"
                               .value="${this.runtimeFreqCount}"
                               @keypress="${_uintInput}"/>
                        <label for="runtime-freq-count">Channel Count</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="runtime-freq-label" type='text' maxlength="12"
                               @change="${(e) => this.runtimeFreqLabel = e.target.value}"
                               .value="${this.runtimeFreqLabel}">
                        <label for="runtime-freq-label">Display Label</label>
                    </div>
                    <div class="mui-checkbox">
                        <input id='runtime-high-freq-enabled' type='checkbox'
                               ?checked="${this.runtimeHighFreqEnabled}"
                               @change="${(e) => this.runtimeHighFreqEnabled = e.target.checked}"
                        />
                        <label for="runtime-high-freq-enabled">Enable runtime high-band override (2.4/S-band/crossband)</label>
                    </div>
                    ${this.runtimeHighFreqEnabled ? html`
                    <div class="mui-textfield">
                        <input id="runtime-high-freq-preset" type='number' min="0" max="4"
                               @change="${(e) => this.runtimeHighFreqPreset = parseInt(e.target.value)}"
                               .value="${this.runtimeHighFreqPreset}"
                               @keypress="${_uintInput}"/>
                        <label for="runtime-high-freq-preset">High-Band Preset Slot</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="runtime-high-freq-start" type='number' min="1900000000" max="2500000000"
                               @change="${(e) => this.runtimeHighFreqStart = parseInt(e.target.value)}"
                               .value="${this.runtimeHighFreqStart}"
                               @keypress="${_uintInput}"/>
                        <label for="runtime-high-freq-start">High-Band Start Frequency (Hz)</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="runtime-high-freq-stop" type='number' min="1900000000" max="2500000000"
                               @change="${(e) => this.runtimeHighFreqStop = parseInt(e.target.value)}"
                               .value="${this.runtimeHighFreqStop}"
                               @keypress="${_uintInput}"/>
                        <label for="runtime-high-freq-stop">High-Band Stop Frequency (Hz)</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="runtime-high-freq-count" type='number' min="4" max="80"
                               @change="${(e) => this.runtimeHighFreqCount = parseInt(e.target.value)}"
                               .value="${this.runtimeHighFreqCount}"
                               @keypress="${_uintInput}"/>
                        <label for="runtime-high-freq-count">High-Band Channel Count</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="runtime-high-freq-label" type='text' maxlength="12"
                               @change="${(e) => this.runtimeHighFreqLabel = e.target.value}"
                               .value="${this.runtimeHighFreqLabel}">
                        <label for="runtime-high-freq-label">High-Band Display Label</label>
                    </div>
                    ` : ''}
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
                'runtime-freq-enabled': this.runtimeFreqEnabled,
                'runtime-freq-preset': this.runtimeFreqPreset,
                'runtime-freq-start': this.runtimeFreqStart,
                'runtime-freq-stop': this.runtimeFreqStop,
                'runtime-freq-count': this.runtimeFreqCount,
                'runtime-freq-label': this.runtimeFreqLabel,
                'runtime-high-freq-enabled': this.runtimeHighFreqEnabled,
                'runtime-high-freq-preset': this.runtimeHighFreqPreset,
                'runtime-high-freq-start': this.runtimeHighFreqStart,
                'runtime-high-freq-stop': this.runtimeHighFreqStop,
                'runtime-high-freq-count': this.runtimeHighFreqCount,
                'runtime-high-freq-label': this.runtimeHighFreqLabel,
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
        changed |= this.runtimeFreqEnabled !== (elrsState.options['runtime-freq-enabled'] || false)
        changed |= this.runtimeFreqPreset !== (elrsState.options['runtime-freq-preset'] || 0)
        changed |= this.runtimeFreqStart !== (elrsState.options['runtime-freq-start'] || 903500000)
        changed |= this.runtimeFreqStop !== (elrsState.options['runtime-freq-stop'] || 926900000)
        changed |= this.runtimeFreqCount !== (elrsState.options['runtime-freq-count'] || 40)
        changed |= this.runtimeFreqLabel !== (elrsState.options['runtime-freq-label'] || '')
        changed |= this.runtimeHighFreqEnabled !== (elrsState.options['runtime-high-freq-enabled'] || false)
        changed |= this.runtimeHighFreqPreset !== (elrsState.options['runtime-high-freq-preset'] || 0)
        changed |= this.runtimeHighFreqStart !== (elrsState.options['runtime-high-freq-start'] || 2400400000)
        changed |= this.runtimeHighFreqStop !== (elrsState.options['runtime-high-freq-stop'] || 2479400000)
        changed |= this.runtimeHighFreqCount !== (elrsState.options['runtime-high-freq-count'] || 80)
        changed |= this.runtimeHighFreqLabel !== (elrsState.options['runtime-high-freq-label'] || '')
        // /FEATURE: HAS_SUBGHZ
        changed |= this.lockOnFirst !== elrsState.options['lock-on-first-connection']
        changed |= this.enableModelMatch && this.modelId !== elrsState.config['modelid']
        changed |= !this.enableModelMatch && this.modelId !== 255
        changed |= this.forceTlmOff !== elrsState.config['force-tlm']
        return !!changed
    }
}

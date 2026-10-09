import { afterEach, beforeEach, expect, it, vi } from 'vitest';

// setupInput 只装配控件；用 Map 捕获每个开关的 onChanged 回调后直接调用，验证上报的配置键。
const toggles = vi.hoisted(() => new Map<string, (active: boolean) => void>());

vi.mock('./shared', () => ({
  applyDropdownValue: vi.fn(),
  applyToggleState: vi.fn(),
  setFuzzyRuleOptionsDisabled: vi.fn(),
  setSmartPunctuationOptionsDisabled: vi.fn(),
  setupDropdownMenu: vi.fn(),
  setupToggleButton: (id: string, onChanged: (active: boolean) => void) => { toggles.set(id, onChanged); }
}));
vi.mock('./config-sync', () => ({ updateConfig: vi.fn() }));
vi.mock('./appearance', () => ({ updateCandidatePreviewHelpcode: vi.fn() }));
vi.mock('./credential-test', () => ({ setupCredentialTest: vi.fn() }));

import { updateConfig } from './config-sync';
import { setSmartPunctuationOptionsDisabled } from './shared';
import { applyCustomTranslationConfig, applyGlmTranslationConfig, applyNiuTransConfig, setupInput } from './input';
import { setupCredentialTest } from './credential-test';

class StubElement extends EventTarget {
  value = '';
  type = 'password';
  title = '';
  textContent = '';
  hidden = false;
  dataset: Record<string, string> = {};
  closest(): StubElement { return this; }
  select(value: string): void {
    this.dataset.value = value;
    this.dispatchEvent(new Event('click'));
  }
  classes = new Set<string>();
  attributes = new Map<string, string>();
  classList = {
    toggle: (name: string, force?: boolean) => {
      const next = force ?? !this.classes.has(name);
      if (next) this.classes.add(name);
      else this.classes.delete(name);
    }
  };
  getAttribute(name: string): string | null { return this.attributes.get(name) ?? null; }
  setAttribute(name: string, value: string): void { this.attributes.set(name, value); }
}

let expand: StubElement;
let details: StubElement;
let elements: Map<string, StubElement>;

beforeEach(() => {
  toggles.clear();
  vi.clearAllMocks();
  expand = new StubElement();
  details = new StubElement();
  elements = new Map([
    'translationProviderMenu', 'tencentTranslationFields', 'niutransTranslationFields',
    'customTranslationFields', 'glmTranslationFields', 'glmTranslationEndpoint',
    'glmTranslationApiKey', 'glmTranslationApiKeyVisibility', 'glmTranslationModel',
    'candidateTranslationApiWarning'
  ].map(id => [id, new StubElement()]));
  vi.stubGlobal('document', {
    querySelectorAll: () => [],
    querySelector: () => null,
    getElementById: (id: string) => {
      if (id === 'smartPunctuationExpand') return expand;
      if (id === 'smartPunctuationDetails') return details;
      return elements.get(id) ?? null;
    },
    addEventListener: vi.fn()
  });
  vi.stubGlobal('window', { chrome: { webview: { postMessage: vi.fn() } } });
  setupInput();
  applyCustomTranslationConfig(undefined);
  applyNiuTransConfig(undefined);
  applyGlmTranslationConfig(undefined);
});

afterEach(() => vi.unstubAllGlobals());

it('reports every smart punctuation sub-switch to its config path', () => {
  const options: [string, string][] = [
    ['smartPunctuationSpaceConvertToggleBtn', 'input.smart_punctuation_space_convert'],
    ['smartPunctuationRepeatToChineseToggleBtn', 'input.smart_punctuation_repeat_to_chinese'],
    ['smartPunctuationDirectDigitToggleBtn', 'input.smart_punctuation_direct_digit'],
    ['smartPunctuationDirectLetterToggleBtn', 'input.smart_punctuation_direct_letter']
  ];
  for (const [id, path] of options) {
    toggles.get(id)?.(true);
    expect(updateConfig).toHaveBeenCalledWith(path, true);
  }
});

it('disables the sub-switches together with the master switch', () => {
  toggles.get('smartPunctuationToggleBtn')?.(false);
  expect(updateConfig).toHaveBeenCalledWith('input.smart_punctuation', false);
  expect(setSmartPunctuationOptionsDisabled).toHaveBeenCalledWith(true);
  toggles.get('smartPunctuationToggleBtn')?.(true);
  expect(setSmartPunctuationOptionsDisabled).toHaveBeenLastCalledWith(false);
});

it('toggles the details container from the section header', () => {
  expand.dispatchEvent(new Event('click'));
  expect(expand.getAttribute('aria-expanded')).toBe('true');
  expect(details.classes.has('open')).toBe(true);
  expand.dispatchEvent(new Event('click'));
  expect(expand.getAttribute('aria-expanded')).toBe('false');
  expect(details.classes.has('open')).toBe(false);
});

it('selects GLM exclusively and preserves its values when switching providers', () => {
  applyGlmTranslationConfig({
    enabled: false, api_key: 'saved-glm-key',
    endpoint: 'https://glm.example.test/v1/chat/completions', model: 'glm-custom-model'
  });
  elements.get('translationProviderMenu')!.select('glm');
  expect(updateConfig).toHaveBeenCalledWith('glm_translation.enabled', true);
  expect(updateConfig).toHaveBeenCalledWith('niutrans.enabled', false);
  expect(updateConfig).toHaveBeenCalledWith('custom_translation.enabled', false);
  expect(elements.get('glmTranslationFields')!.hidden).toBe(false);
  expect(elements.get('tencentTranslationFields')!.hidden).toBe(true);
  elements.get('glmTranslationApiKeyVisibility')!.dispatchEvent(new Event('click'));
  expect(elements.get('glmTranslationApiKey')!.type).toBe('text');
  elements.get('translationProviderMenu')!.select('niutrans');
  expect(updateConfig).toHaveBeenCalledWith('glm_translation.enabled', false);
  expect(elements.get('glmTranslationApiKey')!.type).toBe('password');
  elements.get('translationProviderMenu')!.select('glm');
  expect(elements.get('glmTranslationApiKey')!.value).toBe('saved-glm-key');
  expect(elements.get('glmTranslationEndpoint')!.value).toBe('https://glm.example.test/v1/chat/completions');
  expect(elements.get('glmTranslationModel')!.value).toBe('glm-custom-model');
});

it('prefers GLM in a conflicting snapshot and tests its default endpoint and model', () => {
  applyGlmTranslationConfig({ enabled: true, api_key: 'glm-key', endpoint: '', model: '' });
  applyNiuTransConfig({ enabled: true });
  applyCustomTranslationConfig({ enabled: true });
  expect(elements.get('glmTranslationFields')!.hidden).toBe(false);
  const [, , service, readConfig] = vi.mocked(setupCredentialTest).mock.calls[0];
  expect(service()).toBe('translation.glm');
  expect(readConfig()).toEqual({
    endpoint: 'https://open.bigmodel.cn/api/paas/v4/chat/completions',
    apiKey: 'glm-key', model: 'glm-5.3-flashx'
  });
  expect(elements.get('candidateTranslationApiWarning')!.classes.has('is-hidden')).toBe(true);
});

it('saves edited GLM fields, restores blank defaults and shows missing-key feedback', () => {
  applyGlmTranslationConfig({ enabled: true, api_key: 'key' });
  const endpoint = elements.get('glmTranslationEndpoint')!;
  endpoint.value = ' https://glm.example.test/v1/chat/completions ';
  endpoint.dispatchEvent(new Event('change'));
  expect(updateConfig).toHaveBeenCalledWith('glm_translation.endpoint', 'https://glm.example.test/v1/chat/completions');
  const model = elements.get('glmTranslationModel')!;
  model.value = '';
  model.dispatchEvent(new Event('change'));
  expect(updateConfig).toHaveBeenCalledWith('glm_translation.model', '');
  expect(model.value).toBe('glm-5.3-flashx');
  const key = elements.get('glmTranslationApiKey')!;
  key.value = '';
  key.dispatchEvent(new Event('change'));
  expect(updateConfig).toHaveBeenCalledWith('glm_translation.api_key', '');
  expect(elements.get('candidateTranslationApiWarning')!.classes.has('is-hidden')).toBe(false);
});
